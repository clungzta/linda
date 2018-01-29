
#include <Pozyx.h>
#include <Pozyx_definitions.h>
#include <Wire.h>
#include <IRremote.h>

/*
"Zombie Mode" is intended for Homing the robot to its docking station for a critical battery recharge
Main computer gets turned OFF to conserve ~35W of power.
Therefore: all localisation and navigation is performed ONBOARD OF THE ESP32
Assisted teleop gets turned ON when transitioning to this state.
Refer to README.md for more info about ZOMBIE_MODE
*/

/**************** SUB-STATES *********************/
#define ZOMBIE_MODE_DISABLED_STATE 0
#define ZOMBIE_MODE_GPS_STATE      1
#define ZOMBIE_MODE_POZYX_STATE    2
#define ZOMBIE_MODE_IR_STATE       3
#define ZOMBIE_MODE_HOMED_STATE    4

/******************* CONFIG **********************/

// How fast do we want our Zombie to go?
#define ZOMBIE_MAX_SPEED 0.4 // m/s
#define ZOMBIE_DOCKING_SPEED 0.25 // m/s as it enters the dock

#define GPS_DIST_THRESHOLD_MIN 10000
#define INFRARED_DIST_THRESHOLD_MAX 3000

// what's the name of the hardware serial port for the GPS?
#define GPSSerial Serial2

// what's the name of the hardware serial port for the DW-1000 Tag?
#define RADARSerial Serial1

// FIXME: Pins for the IR reciever interupts
#define IR_PIN_FRONT_LEFT   3
#define IR_PIN_FRONT_CENTRE 4
#define IR_PIN_FRONT_RIGHT  5
#define IR_PIN_REAR_CENTRE  6

// Get these from the manufacturer
// DW-1000 ToF Tags
// the network id of the anchors: change these to the network ids of your anchors.
// Centre of the docking station is the origin
#define RADAR_TAG_ID_LEFT  0x1156
#define RADAR_TAG_ID_RIGHT 0x256B
#define RADAR_TAG_ID_REAR  0x3325

#define RADAR_TAG_SEPARATION     2500 // mm

#define POSITIONING_ALGORITHM POZYX_POS_ALG_UWB_ONLY // try POZYX_POS_ALG_TRACKING for fast moving objects.
#define POZYX_DIMENSIONS      POZYX_2D
#define POZYX_HEIGHT          500 // HEIGHT not used in POZYX_2D

/**************************************************/

// Other Defines
#define RADAR_DIST_THRESHOLD_MAX GPS_DIST_THRESHOLD_MIN
#define RADAR_DIST_THRESHOLD_MIN INFRARED_DIST_THRESHOLD_MAX
#define PI 3.14159265

// Initialisation of IR recievers
IRrecv irrecv_fl(IR_PIN_FRONT_LEFT);
IRrecv irrecv_fc(IR_PIN_FRONT_CENTRE);
IRrecv irrecv_fr(IR_PIN_FRONT_RIGHT);
IRrecv irrecv_rc(IR_PIN_REAR_CENTRE);

uint16_t tags[num_tags] = {0x0001};
uint16_t anchors[num_anchors] = {RADAR_TAG_ID_LEFT, RADAR_TAG_ID_RIGHT, RADAR_TAG_ID_REAR}; // the network id of the anchors: change these to the network ids of your anchors.
int32_t anchors_x[num_anchors] = {-TAG_SEPARATION, 0, TAG_SEPARATION};                      // anchor x-coorindates in mm
int32_t anchors_y[num_anchors] = {0, TAG_SEPARATION, 0};                                    // anchor y-coordinates in mm

void print_pozyx_coordinates(coordinates_t coor)
{
    Serial.print("POS ID 0x");
    Serial.print(network_id, HEX);
    Serial.print(", x(mm): ");
    Serial.print(coor.x);
    Serial.print(", y(mm): ");
    Serial.print(coor.y);
    Serial.print(", z(mm): ");
    Serial.println(coor.z);
}

// function to manually set the Pozyx anchor coordinates
void setAnchorsManual()
{
    for (int i = 0; i < num_anchors; i++)
    {
        device_coordinates_t anchor;
        anchor.network_id = anchors[i];
        anchor.flag = 0x1;
        anchor.pos.x = anchors_x[i];
        anchor.pos.y = anchors_y[i];
        anchor.pos.z = heights[i];
        Pozyx.addDevice(anchor, remote_id);
    }
    if (num_anchors > 4)
    {
        Pozyx.setSelectionOfAnchors(POZYX_ANCHOR_SEL_AUTO, num_anchors, remote_id);
    }
}

class ZombieController
{
    public:
      ZombieController(Adafruit_GPS *gps);
      Velocity run(double target_lat, double target_lon, uint16_t anchors[]);
      void stop();
      bool set_current_state(uint8_t new_state_id);

    private:
      uint8_t current_state_id_;
      bool pozyx_ok_;
      Adafruit_GPS *gps_;
      void init_pozyx_();

      //   double _radar_threshold_max;
      //   double _infrared_threhold_max;
};

ZombieController::ZombieController()
{
    _current_state_id = ZOMBIE_MODE_DISABLED_STATE;

    // Call separate init function from constructor (fixes errors)
    this->init_pozyx();
}

/**
 * @brief This gets called on state transition to ZOMBIE_MODE_STATE
 * 
 * @param target_lat defines the latitude of the docking station
 * @param target_lon defines the longitude of the docking station
 * @param anchors defines the ID's of the Pozyx DW1000 anchor tags
 * These should be positioned relative to the docking station
 * in accordance with the diagram in README.md
 * 
 * TODO: draw working diagram for README.md showing the required tag locations
 * 
 * @return Velocity 
 */
bool ZombieController::set_target(double target_lat, double target_lon, uint16_t anchors[])
{
    // compute intermediate targets?
    return vel;
}

/**
 * @brief When in ZOMBIE_MODE: this function gets called from the main alexbot class in the loop
 * i.e. from "process_command()" 
 * 
 * This generates a Velocity vector which should guide the robot to the target (docking station) 
 * 
 * @return Velocity to be sent to the SEPF Assisted Teleop Controller
 */

// TODO: Add hysteresis to state changes to prevent unwanted rapid switching
Velocity ZombieController::run()
{
    Velocity vel;

    switch (_current_state_id)
    {
        case ZOMBIE_MODE_DISABLED_STATE:
        {
            vel.linear = 0.0;
            vel.angular = 0.0;
        }
        case ZOMBIE_MODE_GPS_STATE:
        {
            // Automatically attempt to change from GPS to DW1000 RADAR state when we get within 10m of the target
            if (gps_dist_to_target <= GPS_DIST_THRESHOLD_MIN)
            {
                if (pozyx_ok_)
                {
                    set_current_state(ZOMBIE_MODE_POZYX_STATE)
                }
            }

            // Use Proportional Controller? Add gyro + encoders?
            vel.linear = constrain(1.0 * gps_dist_to_target, -ZOMBIE_MAX_SPEED, ZOMBIE_MAX_SPEED);
            vel.angular = 0.3 * gps_delta_theta;
        }

        case ZOMBIE_MODE_POZYX_STATE:
        {
            // Automatically attempt to change from DW1000 RADAR to IR state when we get within 3m of the target
            if (pozyx_dist_to_target <= RADAR_DIST_THRESHOLD_MIN)
            {
                set_current_state(ZOMBIE_MODE_IR_STATE)
            }

            // Use Proportional Controller? Add gyro + encoders?
            vel.linear = constrain(1.0 * pozyx_dist_to_target, -ZOMBIE_MAX_SPEED, ZOMBIE_MAX_SPEED);
            vel.angular = 0.3 * pozyx_delta_theta; // rad/s
        }

        case ZOMBIE_MODE_IR_STATE:
        {
            // Use PID Controller? Add gyro + encoders?
            // Get Distance from LIDAR or Ultrasonics?
            // Use EKF?

            // Do not move forward until we are sufficiently aligned with the dock
            if (pozyx_delta_theta < (PI / 8.0))
            {
                vel.linear = constrain(0.1 * pozyx_dist_to_target, -ZOMBIE_DOCKING_SPEED, ZOMBIE_DOCKING_SPEED);
            }
            else
            {
                vel.linear = 0.0;
            }

            vel.angular = 0.3 * ir_delta_theta; // rad/s
        }
    }

    return vel;
}


void ZombieController::stop()
{
    set_current_state_id(ZOMBIE_MODE_DISABLED_STATE);
}

bool ZombieController::set_current_state(uint8_t new_state_id)
{
    Serial.print("Changing ZombieController state from ");
    Serial.print(_current_state_id);
    Serial.print(" to ");
    Serial.println(new_state_id);
    set_current_state(new_state_id);
    return true;
}

void ZombieController::init_pozyx()
{
    // init the Pozyx DW1000 ToF RADAR localisation here
    serial.println("Initialising POZYX")
    pozyx_ok_ = false;

    if (Pozyx.begin() == POZYX_FAILURE)
    {
        Serial.println(F("ERROR: Unable to connect to POZYX shield"));
        Serial.println(F("Reset required"));
        delay(100);
        // abort();
    }

    // sets the anchor manually
    setAnchorsManual();
    // sets the positioning algorithm
    Pozyx.setPositionAlgorithm(POSITIONING_ALGORITHM, POZYX_DIMENSIONS, NULL);
}

coordinates_t ZombieController::get_poxyx_position()
{
    coordinates_t position;
    int status = Pozyx.doPositioning(&position, POZYX_DIMENSIONS, POZYX_HEIGHT, POSITIONING_ALGORITHM);

    if (status != POZYX_SUCCESS)
    {
        uint8_t error_code;
        Pozyx.getErrorCode(&error_code);
        Serial.print("Pozyx Positioning Error: ");
        Serial.println(error_code, HEX);
        pozyx_ok_ = false;
    }

    pozyx_ok_ = true;

    return position;
}

void ZombieController::get_gps_update()
{
    // get update from the GPS reciever here
    // gps_->lat;
    // gps_->lon;
    // gps_->angle;
    // gps_->satellites;

    // ensure everything is in rad
    // compute dist_to_target and angle_to_target (haversine formula)
    // compute delta_theta between angle_to_target and current heading
    // return dist_to_target, delta_theta
}

double ZombieController::compute_docking_station_angle_IR()
{
    // use an interupt pin for each IR reciever (FRONT_LEFT, FRONT_CENTRE FRONT_RIGHT, REAR_CENTRE)
    // pulsein is slow: https://arduino.stackexchange.com/questions/318/how-precise-is-the-timing-of-pulsein
    // TIMING DIAGRAM: https://photos.app.goo.gl/DKFhUayQKq2sYl8x1

    return 0.0
}