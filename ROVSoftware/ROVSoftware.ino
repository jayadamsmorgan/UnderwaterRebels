#include <Ethernet2.h>
#include <EthernetUdp2.h>
#include <Wire.h>
#include <Servo.h>
#include <PID_v1.h>
#include <SparkFun_MS5803_I2C.h>

typedef unsigned uint;
typedef unsigned char uchar;

//#define GY80
#define GY89

#ifdef GY89
#include <Kalman.h>
#include <L3G.h>
#include <LSM303.h>

#define TWI_FREQ 400000L

Kalman kalmanX; // Create the Kalman instances
Kalman kalmanY;

LSM303 compass;
L3G gyro;
float gyroOffSet[3];
int accOffSet[3];

double accG[3];
double gyroSpeed[3];
double mag[3];

double gyroXangle, gyroYangle; // Angle calculate using the gyro only
double compAngleX, compAngleY; // Calculated angle using a complementary filter
double kalAngleX, kalAngleY; // Calculated angle using a Kalman filter

uint32_t timer;
double temperature;
#endif

#ifdef GY80
#include <ADXL345.h>
#include <HMC5883L.h>

ADXL345 accelerometer;
HMC5883L compass;
#endif

#define MOTOR1PIN                2
#define MOTOR2PIN                0
#define MOTOR3PIN                1
#define MOTOR4PIN                5
#define MOTOR5PIN                4
#define MOTOR6PIN                3

#define MOTORLOWMICROSECONDS     1465
#define MOTORHIGHMICROSECONDS    1510
#define MOTORRANGE               500

#define TURBO_SPEED_K            1.0
#define HIGH_SPEED_K             0.5
#define MID_SPEED_K              0.25
#define LOW_SPEED_K              0.10

#define MAIN_MANIP_ROT_PINA      19
#define MAIN_MANIP_ROT_PINB      18

#define MAIN_MANIP_TIGHT_PINA    40
#define MAIN_MANIP_TIGHT_PINB    41

#define MULTIPLEXOR_PIN          A0

#define LED_PIN                  11

#define SERVO_MANIPULATOR_PIN    6
#define SERVO_CAMERA_PIN         13

#define SERVO_ANGLE_DELTA        1

#define MIN_CAMERA_ANGLE         20
#define MAX_CAMERA_ANGLE         160

#define MAX_BOTTOM_MANIP_ANGLE   150
#define MIN_BOTTOM_MANIP_ANGLE   30

#define INCOMING_PACKET_SIZE     25
#define OUTCOMING_PACKET_SIZE    15

double PITCH_KP =               0.8;
double PITCH_KI =               0.0;
double PITCH_KD =               0.5;

double DEPTH_KP =               5.0;
double DEPTH_KI =               0.0;
double DEPTH_KD =               0.0;

double YAW_KP   =               2.0;
double YAW_KI   =               0.0;
double YAW_KD   =               0.0;

int MOTORMIDMICROSECONDS = (MOTORLOWMICROSECONDS + MOTORHIGHMICROSECONDS) / 2.0;

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(192, 168, 1, 177), remote_device;
char packetBuffer[INCOMING_PACKET_SIZE];
unsigned char replyBuffer[OUTCOMING_PACKET_SIZE];
EthernetUDP Udp;

Servo horMotor1, horMotor2, horMotor3, horMotor4;
Servo verMotor1, verMotor2;

Servo camera, bottomManip;
int camera_angle, new_camera_angle, bottom_manip_angle, new_bottom_manip_angle;
unsigned long long prev_camera_servo_update, prev_manip_servo_update;

char servoCamDir = 0, manTightDir = 0, botManipDir = 0;

double declinationAngle = (4.0 + (26.0 / 60.0)) / (180 / M_PI);
double yaw = 0, pitch = 0, roll = 0;
int depth = 0;

// Create variables to store results for depth calculations
double pressure_abs, pressure_baseline;

// Variable for storing joystick values
signed char js_val[5];
bool buttons[8];

// Auto modes
bool isAutoDepth = false, isAutoPitch = false, isAutoYaw = false;

// LED switch
bool isLED = false;

// Mux channels
unsigned char muxChannel = 0;

// Array for leak sensors values
bool leak[8];

// Speed mode for arranging speed
double speedK = 0.3;

MS5803 sensor(ADDRESS_LOW);
int pressureReadings[5];
char depthCounter = 0;

// PIDs for auto modes
double pitchSetpoint, pitchInput, pitchOutput;
PID autoPitchPID(&pitchInput, &pitchOutput, &pitchSetpoint, PITCH_KP, PITCH_KI, PITCH_KD, DIRECT);
double depthSetpoint, depthInput, depthOutput;
PID autoDepthPID(&depthInput, &depthOutput, &depthSetpoint, DEPTH_KP, DEPTH_KI, DEPTH_KD, DIRECT);
double yawSetpoint, yawInput, yawOutput;
PID autoYawPID(&yawInput, &yawOutput, &yawSetpoint, YAW_KP, YAW_KI, YAW_KD, DIRECT);

// Function for controlling motor system
void controlPeripherals() {
  // Auto modes realization:
  if (isAutoPitch && isAutoDepth && js_val[2] == 0) {
    autoPitchAndDepth();
  } else if (isAutoPitch && js_val[2] == 0) {
    autoPitch();
    depthSetpoint = depth; // Set target for AutoDepth
  } else if (isAutoDepth && js_val[2] == 0) {
    autoDepth();
  } else {
    // Set vertical thrust
    verticalMotorControl(verMotor1, js_val[2]);
    verticalMotorControl(verMotor2, js_val[2]);

    // Set target for AutoDepth
    depthSetpoint = depth;
  }

  // AutoYaw mode realization:
  if (isAutoYaw && js_val[0] == 0 && js_val[1] == 0 && js_val[3] == 0) {
    autoYaw();
  } else {
    // Set horizontal thrust
    horizontalMotorControl(horMotor1, -js_val[0], js_val[1], js_val[3], false);
    horizontalMotorControl(horMotor2, -js_val[0], js_val[1], -js_val[3], false);
    horizontalMotorControl(horMotor3, -js_val[0], -js_val[1], js_val[3], false);
    horizontalMotorControl(horMotor4, -js_val[0], -js_val[1], -js_val[3], false);

    // Set target for AutoYaw
    yawSetpoint = yaw;
  }

  // Rotate & tight manipulator
  rotateManipulator(js_val[4]);
  tightenManipulator(manTightDir);

  if (servoCamDir != 0) {
    if (servoCamDir > 0) {
      new_camera_angle += SERVO_ANGLE_DELTA;
      if (new_camera_angle > MAX_CAMERA_ANGLE)
        new_camera_angle = MAX_CAMERA_ANGLE;
    } else {
      new_camera_angle -= SERVO_ANGLE_DELTA;
      if (new_camera_angle < MIN_CAMERA_ANGLE)
        new_camera_angle = MIN_CAMERA_ANGLE;
    }
  }
  if (camera_angle != new_camera_angle) {
    camera.write(new_camera_angle);
    camera_angle = new_camera_angle;
  }

  if (botManipDir != 0) {
    if (botManipDir > 0) {
      new_bottom_manip_angle += SERVO_ANGLE_DELTA;
      if (new_bottom_manip_angle > MAX_BOTTOM_MANIP_ANGLE)
        new_bottom_manip_angle = MAX_BOTTOM_MANIP_ANGLE;
    } else {
      new_bottom_manip_angle -= SERVO_ANGLE_DELTA;
      if (new_bottom_manip_angle < MIN_BOTTOM_MANIP_ANGLE)
        new_bottom_manip_angle = MIN_BOTTOM_MANIP_ANGLE;
    }
  }
  if (bottom_manip_angle != new_bottom_manip_angle) {
    bottomManip.write(new_bottom_manip_angle);
    bottom_manip_angle = new_bottom_manip_angle;
  }

  // Select multiplexor channel for right video out
  selectMuxChannel();

  // Switch on/off LED
  switchLED();
}

// AutoPitch & AutoDepth mode
void autoPitchAndDepth() {

  depthInput = depth;
  pitchInput = pitch;

  autoPitchPID.Compute();
  autoDepthPID.Compute(); 

  double output1, output2;

  output1 = depthOutput + pitchOutput;
  output2 = depthOutput - pitchOutput;

  // Value correction:
  if (output1 > 100.0) {
    output1 = 100.0;
  }
  if (output1 < -100.0) {
    output1 = -100.0;
  }
  if (output2 > 100.0) {
    output2 = 100.0;
  }
  if (output2 < -100.0) {
    output2 = -100.0;
  }

  verticalMotorControl(verMotor1, (char) output1);
  verticalMotorControl(verMotor2, (char) output2);
}

// AutoPitch mode
void autoPitch() {
  pitchInput = pitch;
  autoPitchPID.Compute();

  // Value correction:
  if (pitchOutput > 100.0) {
    pitchOutput = 100.0;
  }
  if (pitchOutput < -100.0) {
    pitchOutput = -100.0;
  }

  verticalMotorControl(verMotor1, (char) -pitchOutput);
  verticalMotorControl(verMotor2, (char) pitchOutput);
}

// AutoDepth mode
void autoDepth() {
  depthInput = depth;
  autoDepthPID.Compute();

  // Value correction:
  if (depthOutput > 100.0) {
    depthOutput = 100.0;
  }
  if (depthOutput < -100.0) {
    depthOutput = -100.0;
  }

  verticalMotorControl(verMotor1, (char) depthOutput);
  verticalMotorControl(verMotor2, (char) depthOutput);
}

// AutoYaw mode
void autoYaw() {
  yawInput = yaw;
  autoYawPID.Compute();
  horizontalMotorControl(horMotor1, 0, 0, yawOutput, true);
  horizontalMotorControl(horMotor2, 0, 0, -yawOutput, true);
  horizontalMotorControl(horMotor3, 0, 0, yawOutput, true);
  horizontalMotorControl(horMotor4, 0, 0, -yawOutput, true);
}

// Function for correct angles for PID (calcuating minimal angle for rotation)
double rotationAngle(double currentAngle, double targetAngle) {
  double rotationAngle = currentAngle - targetAngle;
  if (rotationAngle >= 180.00) {
    rotationAngle = rotationAngle - 360.00;
  } else if (rotationAngle < -180.00) {
    rotationAngle = 360.00 - abs(rotationAngle);
  }
  return rotationAngle; // -180 < rotationAngle <= 180
}

// Receiving messages from PC & parsing
char receiveMessage() {
  int packetSize = Udp.parsePacket();
  if (packetSize == INCOMING_PACKET_SIZE) {
    remote_device = Udp.remoteIP();
    Udp.read(packetBuffer, INCOMING_PACKET_SIZE);

    // STARTING PARSING PACKET ********
    for (int i = 0; i < 5; ++i)  {
      js_val[i] = (signed char)packetBuffer[i];
    }
    js_val[2] = -js_val[2];
    for (int i = 0; i < 8; ++i) {
      buttons[i] = (packetBuffer[5] >> i) & 1;
    }

    if (buttons[0] == 0 && buttons[1] == 1) {
      servoCamDir = 1;
    } else if (buttons[0] == 1 && buttons[1] == 0) {
      servoCamDir = -1;
    } else {
      servoCamDir = 0;
    }

    if (buttons[2] == 0 && buttons[3] == 1) {
      manTightDir = -1;
    } else if (buttons[2] == 1 && buttons[3] == 0) {
      manTightDir = 1;
    } else {
      manTightDir = 0;
    }

    if (buttons[4] == 0 && buttons[5] == 1) {
      botManipDir = -1;
    } else if (buttons[4] == 1 && buttons[5] == 0) {
      botManipDir = 1;
    } else {
      botManipDir = 0;
    }

    if (buttons[6] == 1) {
      muxChannel = 1;
    } else {
      muxChannel = 0;
    }

    char bit1 = (packetBuffer[6]) & 1;
    char bit2 = (packetBuffer[6] >> 1) & 1;
    char bit3 = (packetBuffer[6] >> 2) & 1;

    if (bit3) speedK = TURBO_SPEED_K;
    else if (bit2) speedK = HIGH_SPEED_K;
    else if (bit1) speedK = MID_SPEED_K;
    else speedK = LOW_SPEED_K;

    isAutoPitch = (packetBuffer[6] >> 3) & 1;
    isAutoDepth = (packetBuffer[6] >> 4) & 1;
    isAutoYaw = (packetBuffer[6] >> 5) & 1;

    isLED = (packetBuffer[6] >> 6) & 1;

    getK();
    // ENDING PARSING PACKET ********

    return 1;
  } else {
    return 0;
  }
}

// Function to get koefficients from packetBuffer
void getK() {
  YAW_KP = (double) (bytesToUInt(packetBuffer[7], packetBuffer[8]) / 1000);
  YAW_KI = (double) (bytesToUInt(packetBuffer[9], packetBuffer[10]) / 1000);
  YAW_KD = (double) (bytesToUInt(packetBuffer[11], packetBuffer[12]) / 1000);
  PITCH_KP = (double) (bytesToUInt(packetBuffer[13], packetBuffer[14]) / 1000);
  PITCH_KI = (double) (bytesToUInt(packetBuffer[15], packetBuffer[16]) / 1000);
  PITCH_KD = (double) (bytesToUInt(packetBuffer[17], packetBuffer[18]) / 1000);
  DEPTH_KP = (double) (bytesToUInt(packetBuffer[19], packetBuffer[20]) / 1000);
  DEPTH_KI = (double) (bytesToUInt(packetBuffer[21], packetBuffer[22]) / 1000);
  DEPTH_KD = (double) (bytesToUInt(packetBuffer[23], packetBuffer[24]) / 1000);
  autoPitchPID.SetTunings(PITCH_KP, PITCH_KI, PITCH_KD);
  autoYawPID.SetTunings(YAW_KP, YAW_KI, YAW_KD);
  autoDepthPID.SetTunings(DEPTH_KP, DEPTH_KI, DEPTH_KD);
}

int bytesToUInt(byte firstByte, byte secondByte) {
  return (static_cast<uint>(static_cast<uchar>(secondByte)) << 8 ) | static_cast<uint>(static_cast<uchar>(firstByte));
}

// Forming & sending packet to PC via UDP
void sendReply() {
  replyBuffer[0]  = ((int) (yaw * 100.00) >> 8) & 0xFF;
  replyBuffer[1]  = ((int) (yaw * 100.00)) & 0xFF;
  replyBuffer[2]  = ((int) (pitch * 100.00) >> 8) & 0xFF;
  replyBuffer[3]  = ((int) (pitch * 100.00)) & 0xFF;
  replyBuffer[4]  = ((int) (roll * 100.00) >> 8) & 0xFF;
  replyBuffer[5]  = ((int) (roll * 100.00)) & 0xFF;
  replyBuffer[6]  = ((int) (depth) >> 8) & 0xFF;
  replyBuffer[7]  = ((int) (depth)) & 0xFF;
  replyBuffer[8]  = ((int) (yawSetpoint * 100.00) >> 8) & 0xFF;
  replyBuffer[9]  = ((int) (yawSetpoint * 100.00)) & 0xFF;
  replyBuffer[10] = ((int) (depthSetpoint) >> 8 ) & 0xFF;
  replyBuffer[11] = ((int) (depthSetpoint)) & 0xFF;
  for (int i = 0; i < 8; ++i) {
    replyBuffer[12] |= leak[i] << i;
  }
#ifdef GY89
  replyBuffer[13] = ((uint) (temperature) >> 8) & 0xFF;
  replyBuffer[14] = ((uint) (temperature)) & 0xFF;
#endif
  Udp.beginPacket(remote_device, Udp.remotePort());
  Udp.write(replyBuffer, OUTCOMING_PACKET_SIZE);
  Udp.endPacket();
  return;
}

// Function to control horizontal brushless motors
void horizontalMotorControl(Servo motor, short x, short y, short z, bool isAuto) {
  int POW = 0;
  int sum = x + y + z;
  if (sum > 100.0) sum = 100.00;
  if (sum < (-100.0)) sum = -100.00;
  if (isAuto) POW = int((sum * (MOTORRANGE / 100.0)));
  else POW = int((sum * (MOTORRANGE / 100.0)) * speedK);
  if (POW == 0) {
    motor.writeMicroseconds(MOTORMIDMICROSECONDS);
  }
  if (POW < 0) {
    motor.writeMicroseconds(MOTORLOWMICROSECONDS + POW);
  }
  if (POW > 0) {
    motor.writeMicroseconds(MOTORHIGHMICROSECONDS + POW);
  }
}

// Function to control vertical brushless motors
void verticalMotorControl(Servo motor, short z) {
  int POW = 0;
  int sum = z;
  if (sum > 100.0) sum = 100.0;
  if (sum < (-100.0)) sum = -100.0;
  POW = int((sum * (MOTORRANGE / 100.0)) * speedK);
  if (POW == 0) {
    motor.writeMicroseconds(MOTORMIDMICROSECONDS);
  }
  if (POW < 0) {
    motor.writeMicroseconds(MOTORLOWMICROSECONDS + POW);
  }
  if (POW > 0) {
    motor.writeMicroseconds(MOTORHIGHMICROSECONDS + POW);
  }
}

// Function to rotate manipulator
void rotateManipulator(short m) {
  if (m > 0) {
    digitalWrite(MAIN_MANIP_ROT_PINA, HIGH);
    digitalWrite(MAIN_MANIP_ROT_PINB, LOW);
  }
  if (m < 0) {
    digitalWrite(MAIN_MANIP_ROT_PINA, LOW);
    digitalWrite(MAIN_MANIP_ROT_PINB, HIGH);
  }
  if (m == 0) {
    digitalWrite(MAIN_MANIP_ROT_PINA, LOW);
    digitalWrite(MAIN_MANIP_ROT_PINB, LOW);
  }
}

// Function to tight manipulator
void tightenManipulator(char dir) {
  if (dir > 0) {
    digitalWrite(MAIN_MANIP_TIGHT_PINA, HIGH);
    digitalWrite(MAIN_MANIP_TIGHT_PINB, LOW);
  }
  if (dir < 0) {
    digitalWrite(MAIN_MANIP_TIGHT_PINA, LOW);
    digitalWrite(MAIN_MANIP_TIGHT_PINB, HIGH);
  }
  if (dir == 0) {
    digitalWrite(MAIN_MANIP_TIGHT_PINA, LOW);
    digitalWrite(MAIN_MANIP_TIGHT_PINB, LOW);
  }
}

// Setup function
void setup() {
  // Init I2C connection for IMU
  Wire.begin();
  TWBR = ((F_CPU / TWI_FREQ) - 16) / 2;
  
  // Init brushless motors
  pinMode(MOTOR1PIN, OUTPUT);
  pinMode(MOTOR2PIN, OUTPUT);
  pinMode(MOTOR3PIN, OUTPUT);
  pinMode(MOTOR4PIN, OUTPUT);
  pinMode(MOTOR5PIN, OUTPUT);
  pinMode(MOTOR6PIN, OUTPUT);
  horMotor1.attach(MOTOR1PIN);
  horMotor2.attach(MOTOR2PIN);
  horMotor3.attach(MOTOR3PIN);
  horMotor4.attach(MOTOR4PIN);
  verMotor1.attach(MOTOR5PIN);
  verMotor2.attach(MOTOR6PIN);
  delay(500);
  horMotor1.writeMicroseconds(MOTORMIDMICROSECONDS);
  horMotor2.writeMicroseconds(MOTORMIDMICROSECONDS);
  horMotor3.writeMicroseconds(MOTORMIDMICROSECONDS);
  horMotor4.writeMicroseconds(MOTORMIDMICROSECONDS);
  verMotor1.writeMicroseconds(MOTORMIDMICROSECONDS);
  verMotor2.writeMicroseconds(MOTORMIDMICROSECONDS);

  // Ethernet init
  Ethernet.begin(mac, ip);
  Udp.begin(8000);

  pinMode(LED_PIN, OUTPUT);
  pinMode(MAIN_MANIP_ROT_PINA, OUTPUT);
  pinMode(MAIN_MANIP_ROT_PINB, OUTPUT);
  pinMode(MAIN_MANIP_TIGHT_PINA, OUTPUT);
  pinMode(MAIN_MANIP_TIGHT_PINB, OUTPUT);
  pinMode(SERVO_CAMERA_PIN, OUTPUT);
  pinMode(SERVO_MANIPULATOR_PIN, OUTPUT);

  // Init bottom manipulator & main camera
  camera.attach(SERVO_CAMERA_PIN);
  new_camera_angle = 120;
  camera.write(new_camera_angle);
  bottomManip.attach(SERVO_MANIPULATOR_PIN);
  new_bottom_manip_angle = 150;
  bottomManip.write(new_bottom_manip_angle);

  // Init PID settings
  autoPitchPID.SetMode(AUTOMATIC);
  autoPitchPID.SetOutputLimits(-5000, 5000);
  autoDepthPID.SetMode(AUTOMATIC);
  autoDepthPID.SetOutputLimits(-5000, 5000);
  autoYawPID.SetMode(AUTOMATIC);
  autoYawPID.SetOutputLimits(-5000, 5000);
  pitchSetpoint = 0;
  // Some delay for motors...
  delay(1000);
#ifdef GY80
  accelerometer.begin();
  compass.begin();
#endif
#ifdef GY89
  initCompass();
  initgyro();
  delay(100);
  read_Acc();
  read_Gyro();
  read_Mag();
  roll  = atan(accG[1] / sqrt(accG[0] * accG[0] + accG[2] * accG[2])) * RAD_TO_DEG;
  pitch = atan2(-accG[0], accG[2]) * RAD_TO_DEG;
  kalmanX.setAngle(roll); // Set starting angle
  kalmanY.setAngle(pitch);
  gyroXangle = roll;
  gyroYangle = pitch;
  compAngleX = roll;
  compAngleY = pitch;
  timer = micros();
#endif
  // Retrieve calibration constants for conversion math.
  sensor.reset();
  sensor.begin();
  int setupPressure = sensor.getPressure(ADC_2048);
  for (int i = 0; i < 5; i++) {
    pressureReadings[i] = setupPressure;
  }
  depthSetpoint = setupPressure;
}
#ifdef GY89
void read_Acc() { //g
  compass.read();
  byte tl = compass.readReg(LSM303::TEMP_OUT_L);
  byte th = compass.readReg(LSM303::TEMP_OUT_H);
  int temperature_raw = (int16_t)(th << 8 | tl);
  temperature = (double) temperature_raw / 8 + 20;
  accG[0] = ((compass.a.x - accOffSet[0]) >> 4) * 0.004;
  accG[1] = ((compass.a.y - accOffSet[1]) >> 4) * 0.004;
  accG[2] = ((compass.a.z - accOffSet[2]) >> 4) * 0.004;
}

void read_Gyro() { //deg per s
  gyro.read();
  gyroSpeed[0] = (gyro.g.x - gyroOffSet[0]) * 0.07;
  gyroSpeed[1] = (gyro.g.y - gyroOffSet[1]) * 0.07;
  gyroSpeed[2] = (gyro.g.z - gyroOffSet[2]) * 0.07;
}

void read_Mag() { //guass  x,y 450LSB/Guass, z 400LSB/Guass
  mag[0] = compass.m.x / 450.0;
  mag[1] = compass.m.y / 450.0;
  mag[2] = compass.m.z / 400.0;
}

void initCompass() {
  compass.init();
  compass.enableDefault();
  compass.writeReg(LSM303::CTRL_REG4_A, 0x28); // 8 g full scale: FS = 10; high resolution output mode
}

void initgyro() {
  if (!gyro.init())
  {
    while (1);
  }
  gyro.writeReg(L3G::CTRL_REG4, 0x20); // 2000 dps full scale, 70mdeg per LSB
  gyro.writeReg(L3G::CTRL_REG1, 0x0F); // normal power mode, all axes enabled, 95 Hz
}
#endif
// Function for updating depth
void updateDepth() {
  pressureReadings[depthCounter % 5] = sensor.getPressure(ADC_2048);
  if (depthCounter >= 5) {
    depthCounter = 0;
  } else {
    depthCounter++;
  }
  for (int i = 0; i < (5 - 1); i++) {
    for (int o = 0; o < (5 - (i + 1)); o++) {
      if (pressureReadings[o] > pressureReadings[o + 1]) {
        int t = pressureReadings[o];
        pressureReadings[o] = pressureReadings[o + 1];
        pressureReadings[o + 1] = t;
      }
    }
  }

  pressure_abs = pressureReadings[1];
  depth = pressure_abs;
}

// Function for updating yaw, pitch, roll
void updateYPR() {
#ifdef GY80
  Vector mag = compass.readNormalize();
  yaw = atan2(mag.YAxis, mag.XAxis);
  yaw += declinationAngle;
  if (yaw < 0) {
    yaw += 2 * PI;
  }
  if (yaw > 2 * PI) {
    yaw -= 2 * PI;
  }
  yaw = yaw * 180 / M_PI;

  // Reading accelerometer values from IMU; calculating & filtering (median filter) pitch & roll
  double fpitcharray[5];
  double frollarray[5];
  for (int i = 0; i < 5; i++) {
    Vector accl = accelerometer.readNormalize();
    Vector faccl = accelerometer.lowPassFilter(accl, 0.5);
    fpitcharray[i] = -(atan2(faccl.XAxis, sqrt(faccl.YAxis  * faccl.YAxis + faccl.ZAxis * faccl.ZAxis)) * 180.0) / M_PI;
    frollarray[i] = (atan2(faccl.YAxis, faccl.ZAxis) * 180.0) / M_PI;
  }
  for (int i = 0; i < (5 - 1); i++) {
    for (int o = 0; o < (5 - (i + 1)); o++) {
      if (fpitcharray[o] > fpitcharray[o + 1]) {
        int t = fpitcharray[o];
        fpitcharray[o] = fpitcharray[o + 1];
        fpitcharray[o + 1] = t;
      }
    }
  }
  for (int i = 0; i < (5 - 1); i++) {
    for (int o = 0; o < (5 - (i + 1)); o++) {
      if (frollarray[o] > frollarray[o + 1]) {
        int t = frollarray[o];
        frollarray[o] = frollarray[o + 1];
        frollarray[o + 1] = t;
      }
    }
  }

  // Swap pitch & roll because our electronic engineers are very stupid...
  roll = fpitcharray[2];
  pitch = frollarray[2];
#endif
#ifdef GY89
  read_Acc();
  read_Gyro();
  read_Mag();
  double dt = (double)(micros() - timer) / 1000000; // Calculate delta time
  timer = micros();
  roll  = atan(accG[1] / sqrt(accG[0] * accG[0] + accG[2] * accG[2])) * RAD_TO_DEG;
  pitch = atan2(-accG[0], accG[2]) * RAD_TO_DEG;
  double gyroXrate = gyroSpeed[0] / 131.0; // Convert to deg/s
  double gyroYrate = gyroSpeed[1] / 131.0; // Convert to deg/s
  // This fixes the transition problem when the accelerometer angle jumps between -180 and 180 degrees
  if ((pitch < -90 && kalAngleY > 90) || (pitch > 90 && kalAngleY < -90)) {
    kalmanY.setAngle(pitch);
    compAngleY = pitch;
    kalAngleY = pitch;
    gyroYangle = pitch;
  } else
    kalAngleY = kalmanY.getAngle(pitch, gyroYrate, dt); // Calculate the angle using a Kalman filter
  if (abs(kalAngleY) > 90)
    gyroXrate = -gyroXrate; // Invert rate, so it fits the restriced accelerometer reading
  kalAngleX = kalmanX.getAngle(roll, gyroXrate, dt); // Calculate the angle using a Kalman filter
  gyroXangle += gyroXrate * dt; // Calculate gyro angle without any filter
  gyroYangle += gyroYrate * dt;

  compAngleX = 0.93 * (compAngleX + gyroXrate * dt) + 0.07 * roll; // Calculate the angle using a Complimentary filter
  compAngleY = 0.93 * (compAngleY + gyroYrate * dt) + 0.07 * pitch;
  if (gyroXangle < -180 || gyroXangle > 180)
    gyroXangle = kalAngleX;
  if (gyroYangle < -180 || gyroYangle > 180)
    gyroYangle = kalAngleY;
  yaw = compass.heading();
#endif
}

// Function to select right multiplexor channel
void selectMuxChannel() {
  if (muxChannel == 0) {
    digitalWrite(MULTIPLEXOR_PIN, LOW);
  }
  else if (muxChannel == 1) {
    digitalWrite(MULTIPLEXOR_PIN, HIGH);
  }
}

// Function to switch off/on LED
void switchLED() {
  if (isLED) {
    digitalWrite(LED_PIN, HIGH);
  } else {
    digitalWrite(LED_PIN, LOW);
  }
}

// Loop function
void loop() {
  updateYPR();
  updateDepth();
  if (receiveMessage() == 1) {
    sendReply();
  }
  controlPeripherals();
}
