#include "encoder_driver.h"
#include "teleop_controller.h"
#include "motor_velocity_controller.h"
#include "zombie_mode.h"

/***************************** STATE DEFINITIONS **************************************/
// These are the names of the states that the car can be in:

// SLEEP_STATE: Microcontroller enters deep sleep to conserve power
// Main computer gets turned OFF
// In this state: the Wi-Fi is polled occasionally to check whether the robot should "wake up" and do something
#define SLEEP_STATE             0

// HALT_STATE: Microcontroller in normal state, robot motor contollers disabled
// Main computer can be either ON or OFF
#define HALT_STATE              1

// BLUETOOTH_COMMAND_STATE: For use with Bluetooth Joystick android app
// (https://play.google.com/store/apps/details?id=org.projectproto.btjoystick)
// Main computer can be either ON or OFF
// Assisted teleop can be either ON or OFF
#define BLUETOOTH_TELEOP_STATE  2

// ZOMBIE_STATE: "Zombie Mode" is intended for Homing the robot to its docking station for a critical battery recharge
// Main computer gets turned OFF to conserve ~35W of power.
// Therefore: all localisation and navigation is performed ONBOARD OF THE ESP32
// Assisted teleop is ON in this state
// See README.md for more info about ZOMBIE_MODE
#define ZOMBIE_STATE            3

// SERIAL_COMMAND_STATE: for use with ROS or other serial driver
// Receives and processes commands, including velocity command
// Can also adjust config parameters ON the Microcontroller
#define SERIAL_COMMAND_STATE    4

/**************************** ARDUINO PIN DEFINITIONS ********************************/
#define FAILSAFE_PIN       10   // To emergency stop switch
#define FAILSAFE_LED_PIN   13   // OUTPUT TO LED ON THE ARDUINO BOARD

// Motor driver Pins (UART Serial)
#define MOTOR_CONTROLLER_TX 2   // S1 on the sabertooth 2x25A goes to pin 2

#define LEFT_ENCODER_CS_PIN  3
#define RIGHT_ENCODER_CS_PIN 4


/************************************** CONFIG *************************************/

// Maximum allowable power to the motors
#define DRIVE_MOTORS_MAX_POWER 60

// FIXME: Wheel and encoder parameters
#define ENCODER_COUNTS_PER_REV 22000
#define WHEEL_RADIUS 7

/************************************ SERIAL SETUP **********************************/

// invoke the serials in ESP32
HardwareSerial Serial1(1); // pin 12=RX, pin 13=TX
#define SERIAL1_RXPIN 12
#define SERIAL1_TXPIN 13

HardwareSerial Serial2(2); // pin 16=RX, pin 17=TX

// what's the name of the hardware serial port for the GPS?
#define GPSSerial Serial2

// what's the name of the hardware serial port for the Sabertooth?
#define MotorSerial Serial1

/***********************************************************************************/


#define LEFT_MOTOR_ID  0
#define RIGHT_MOTOR_ID 1

// If a command from the RC or AI has not been recieved within WATCHDOG_TIMEOUT ms, will be switched to HALT state.
#define WATCHDOG_TIMEOUT 250

Adafruit_GPS GPS(&GPSSerial);

class AlexbotController
{
  public:
    AlexbotController()
    {
        // Initialise pins
        pinMode(FAILSAFE_LED_PIN, OUTPUT);
        pinMode(FAILSAFE_PIN, INPUT);
    }

    void init() {

        // Seperate function for initialising objects
        // In Arduino, these init calls do not work from the class constructor

        // TODO: MOVE ALL OF THESE "new" (dynamic creation) to global variables to prevent "strange things happening"
        // this keeps the heap unfragmented
        // https://arduino.stackexchange.com/a/17966
        sabertooth = new SabertoothSimplified(MotorSerial);

        // Initialise Wheel Encoders
        left_encoder = new WheelEncoderLS7366(LEFT_MOTOR_ID, LEFT_ENCODER_CS_PIN, ENCODER_COUNTS_PER_REV, WHEEL_RADIUS);
        right_encoder = new WheelEncoderLS7366(RIGHT_MOTOR_ID, RIGHT_ENCODER_CS_PIN, ENCODER_COUNTS_PER_REV, WHEEL_RADIUS);

        // Initialise Motor Controllers
        left_motor = new MotorVelocityController(
            "Left motor", sabertooth, LEFT_MOTOR_ID, left_encoder, DRIVE_MOTORS_MAX_POWER);

        right_motor = new MotorVelocityController(
            "Right motor", sabertooth, RIGHT_MOTOR_ID, right_encoder, DRIVE_MOTORS_MAX_POWER);

        // init 9600 baud comms with GPS reciever
        GPS.begin(9600);

        // Request updates on antenna status, comment out to keep quiet
        GPS.sendCommand(PGCMD_ANTENNA);

        zombie_controller = new ZombieController(&GPS);
    }

    void process_velocity_command(double cmd_x_velocity = 0.0, double cmd_theta = 0.0)
    {
        // This function gets called repeatedly

        last_command_timestamp = millis();
        Serial.println("Processing command");

        // Will be changed into the HALT state if it is not safe to drive.
        check_failsafes();

        // State Machine
        switch (current_state_id)
        {
            case HALT_STATE:
                // We are in HALT_STATE

                break;

            case BLUETOOTH_TELEOP_STATE:
            {
                // We are in RC_TELEOP_STATE
                // Lets act according to the PWM input commands from the RC reciever

                double left_vel_desired = 0.0;
                double right_vel_desired = 0.0;

                Serial.print(", desired_left=");
                Serial.print(left_vel_desired);

                Serial.print(", desired_right=");
                Serial.print(right_vel_desired);

                // FIXME: Change from velocity control to position control
                // Send command to the brake motor controller
                left_motor->SetTargetVelocity(left_vel_desired);
                right_motor->SetTargetVelocity(right_vel_desired);

                Serial.println("");
                break;
            }
        }

    }

    bool set_current_state_ID(uint8_t new_state_id)
    {
        // This function gets called when a change state is requested
        // Returns true on a successful transition

        // Code blocks within this switch statement are only called on change transient
        switch (new_state_id)
        {
            case HALT_STATE:
                // Do nothing on transition into HALT_STATE
                break;
            case BLUETOOTH_TELEOP_STATE:
                // Do nothing on transition into BLUETOOTH_TELEOP_STATE
                break;
            case SERIAL_COMMAND_STATE:
                // Do nothing on transition into SERIAL_COMMAND_STATE
                break;
        }

        Serial.print("Changing state to: ");
        Serial.println(new_state_id);

        current_state_id = new_state_id;
        return true;
    }

    int get_current_state_ID()
    {
        // Return the ID of the state that we are currently in
        return current_state_id;
    }

    float read_pwm_value(int pwm_pin)
    {
        // Read a value from a PWM input
        // Used on RC control for all commands, and failsafe
        // Used on AI control for failsafe ONLY
        unsigned long pwm_time = pulseIn(pwm_pin, HIGH);
        return (float)pwm_time;
    }

    bool check_failsafes()
    {
        // This function will check all failsafes
        // If it is not safe to drive: the car will be switched to HALT_STATE
        // Pin 13 will be ON when it is safe to drive, otherwise OFF.

        // The failsafes include: a watchdog timer (i.e. an automatic shutdown if a command hasn't been recieved within 250ms)
        // Also included is a hardware switch.

        Serial.println("Checking failsafes!");
        bool watchdog_valid = ((millis() - last_command_timestamp) < WATCHDOG_TIMEOUT);
        bool failsafe_switch_engaged = digitalRead(FAILSAFE_PIN);

        Serial.print("failsafe_switch_engaged=");
        Serial.print(failsafe_switch_engaged);

        Serial.print(", watchdog_valid=");
        Serial.println(watchdog_valid);

        bool safe_to_drive = (watchdog_valid && failsafe_switch_engaged);

        if (!safe_to_drive)
        {
            set_current_state_ID(HALT_STATE);
        }

        digitalWrite(FAILSAFE_LED_PIN, safe_to_drive);

        return safe_to_drive;
    }


private:
    int current_state_id;
    long last_command_timestamp;

    SabertoothSimplified *sabertooth;

    MotorVelocityController *left_motor;
    MotorVelocityController *right_motor;

    WheelEncoderLS7366 *left_encoder;
    WheelEncoderLS7366 *right_encoder;

    ZombieController *zombie_controller;
};
