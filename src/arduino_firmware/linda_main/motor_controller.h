

class MotorController
{
    public:
      MotorController(String _my_name, SabertoothSimplified* _motor_interface,
          int _motor_id, int _feedback_pin, int _motor_min_pos, int _motor_max_pos, int _motor_max_power,
          double _Kp = 0.5, double _Ki = 0.0, double _Kd = 0.0);
          
      void SetTargetPosition(double target_pos);

    private:
      String my_name;
      SabertoothSimplified* motor_interface;
      int motor_id;
      int feedback_pin;
      double Kp;
      double Kd;
      double Ki;
      int motor_min_pos;
      int motor_max_pos;
      int motor_max_power;
};

MotorController::MotorController(String _my_name, SabertoothSimplified* _motor_interface,
    int _motor_id, int _feedback_pin, int _motor_min_pos, int _motor_max_pos, int _motor_max_power,
    double _Kp = 0.5, double _Ki = 0.0, double _Kd = 0.0)
{
    // init the motor controller here
    this->my_name         = _my_name;
    this->motor_id        = _motor_id;
    this->motor_interface = _motor_interface;
    this->feedback_pin    = _feedback_pin;
    this->motor_min_pos   = _motor_min_pos;
    this->motor_max_pos   = _motor_max_pos;
    this->motor_max_power = _motor_max_power;
    this->Kp              = _Kp;
    this->Ki              = _Ki;
    this->Kd              = _Kd;
  }

void MotorController::SetTargetPosition(double target_pos)
{
    if (target_pos < motor_min_pos) {
        target_pos = motor_min_pos;
    } else if (target_pos > motor_max_pos) {
        target_pos = motor_max_pos;
    }
    
    double current_pos = double(analogRead(feedback_pin));
    Serial.print(", current_pos=");
    Serial.print(current_pos);

    double pTerm = current_pos - target_pos;
    double iTerm = 0.0;
    double dTerm = 0.0;
    double output = int(Kp * pTerm + Ki * iTerm + Kd * dTerm);

    if ( output < -1 * motor_max_power ) {
      output = -1 * motor_max_power;
    } else if ( output > motor_max_power ) {
      output = motor_max_power;
    }

    Serial.println("");
    Serial.print(my_name);
    Serial.print(", motor ID: ");
    Serial.print(motor_id);
    Serial.print(", output=");
    Serial.print(output);
    Serial.print(", target_pos=");
    Serial.print(target_pos);

    if (abs(output) > 10)
    {
      motor_interface->motor(motor_id, output);
    }
}
