#include <SoftwareSerial.h>
#include <stdint.h>
// #include "LowPower.h"

#define RE 4
#define DE 5

#define BUFF_SIZE   128
#define ONE_MINUTE  60000
#define ONE_HOUR    60 * ONE_MINUTE

String inputString = "";      // a String to hold incoming data
bool stringComplete = false;  // whether the string is complete

long old_time = millis();
long new_time;

// long uplink_interval = 5 * ONE_MINUTE;
// long uplink_interval = 2 * 60 * ONE_MINUTE;
long uplink_interval = 30 * ONE_MINUTE;

long new_interval = uplink_interval;
bool first_time = true;
bool time_to_at_recvb = false;
bool get_LA66_data_status = false;
bool network_joined_status = false;

const byte ammonia[]    = { 0x01, 0x03, 0x00, 0x06, 0x00, 0x01, 0x64, 0x0B };
const byte temp[]       = { 0x01, 0x03, 0x00, 0x01, 0x00, 0x01, 0xD5, 0xCA };
const byte humidity[]   = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A };

const byte inquiry_frame[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x07, 0x04, 0x08 };

SoftwareSerial dragino_shield(10, 11);      // Arduino RX, TX ,
SoftwareSerial sensor(8, 9);                // Arduino RX, TX
const int bat_pin = A0;                     // Analog Battery Pin

char rxbuff[BUFF_SIZE];
uint8_t rxbuff_index = 0;

byte values[19]; // Holds the output of the sensor (index 4 has sensor reading)

uint16_t val1, val2, val3, bat_level;

byte read_parameter(byte * parameter, size_t parameter_size);
byte read_battery_level();
void read_sensor_values();
void print_sensor_data();

union {
    uint32_t combined_value;
    struct {
        uint8_t val1;
        uint8_t val2;
        uint8_t val3;
    } sensor_values;
} sensor_data;

void setup() {
    Serial.begin(9600);
    sensor.begin(9600);
    dragino_shield.begin(9600);
    
    pinMode(RE, OUTPUT);
    pinMode(DE, OUTPUT);

    digitalWrite(RE, HIGH);
    digitalWrite(DE, HIGH);

    // reserve 200 bytes for the inputString:
    inputString.reserve(200);

    dragino_shield.println("ATZ");  //reset LA66
    // LowPower.idle(SLEEP_1S, ADC_OFF, TIMER2_OFF, TIMER1_OFF, TIMER0_ON, SPI_OFF, USART0_OFF, TWI_OFF);
}

void loop() {   
    // LowPower.idle(SLEEP_8S, ADC_OFF, TIMER2_OFF, TIMER1_OFF, TIMER0_ON, SPI_OFF, USART0_OFF, TWI_OFF);
    
    new_time = millis();
    
    if (((new_time - old_time >= uplink_interval) && (network_joined_status == 1)) || (first_time == true && network_joined_status == 1)) {
        old_time = new_time;
        get_LA66_data_status = false;

        first_time = false;

        read_sensor_values();

        Serial.println("---------- Transmitting --------------");
        Serial.print("\tAmmonia: "); Serial.print(val1); Serial.println("ppm");
        Serial.print("\tTemperature: "); Serial.print(val2); Serial.println("°C");
        Serial.print("\tHumidity: "); Serial.print(val3); Serial.println("%");
        Serial.print("\tBattery Level: "); Serial.print(bat_level); Serial.println("%");
        Serial.println("------------------------");
        print_sensor_data();

        dragino_shield.listen();
        delay(650);     // Minimum time to start listening

        char sensor_data_buff[BUFF_SIZE] = "\0";
        snprintf(sensor_data_buff, BUFF_SIZE, "AT+SENDB=%d,%d,%d,%02X%02X%02X%02X", 0, 2, 4, val1, val2, val3, bat_level);
        dragino_shield.println(sensor_data_buff);
    }

    if (time_to_at_recvb == true) {
        time_to_at_recvb = false;
        get_LA66_data_status = true;
        // LowPower.idle(SLEEP_1S, ADC_OFF, TIMER2_OFF, TIMER1_OFF, TIMER0_ON, SPI_OFF, USART0_OFF, TWI_OFF);

        dragino_shield.println("AT+CFG");
    }

    // while ( Serial.available()) {
    //     // get the new byte:
    //     char inChar = (char) Serial.read();
    //     // add it to the inputString:
    //     inputString += inChar;
    //     // if the incoming character is a newline, set a flag so the main loop can
    //     // do something about it:
    //     if (inChar == '\n' || inChar == '\r') {
    //         dragino_shield.print(inputString);
    //         inputString = "\0";
    //     }
    // }

    while (dragino_shield.available()) {
        // get the new byte:
        char inChar = (char)dragino_shield.read();
        // add it to the inputString:
        inputString += inChar;

        rxbuff[rxbuff_index++] = inChar;

        if (rxbuff_index > BUFF_SIZE)
        break;

        // if the incoming character is a newline, set a flag so the main loop can
        // do something about it:
        if (inChar == '\n' || inChar == '\r') {
            stringComplete = true;
            rxbuff[rxbuff_index] = '\0';

            if (strncmp(rxbuff, "JOINED", 6) == 0) {
                network_joined_status = 1;
            }

            if (strncmp(rxbuff, "Dragino LA66 Device", 19) == 0) {
                network_joined_status = 0;
            }

            if (strncmp(rxbuff, "Run AT+RECVB=? to see detail", 28) == 0) {
                time_to_at_recvb = true;
                stringComplete = false;
                inputString = "\0";
            }

            if (strncmp(rxbuff, "AT+RECVB=", 9) == 0) {
                stringComplete = false;
                inputString = "\0";
                String downlink = &rxbuff[9];
                Serial.print("\r\nGet downlink data(FPort & Payload) "); Serial.println(&rxbuff[9]);
                int conf_msg = downlink_action(downlink);
                if (conf_msg == 0) { Serial.print("Changed 'uplink_interval' to: "); Serial.println(new_interval); }
                if (conf_msg == 1) { Serial.println("Device has been reset"); }
            }

            rxbuff_index = 0;

            if (get_LA66_data_status == true) {
                stringComplete = false;
                inputString = "\0";
            }
        }
    }

    // print the string when a newline arrives:
    if (stringComplete) {
        Serial.print(inputString);
        
        // clear the string:
        inputString = "\0";
        stringComplete = false;
    }
}

int downlink_action(String port_payload) {
    // Example:     2:0100012c
    // Example:  port:01234567
    // String port = port_payload.substring(0,1);
    String payload = port_payload.substring(2);
    String type = payload.substring(0, 2);

    if (type == "00") {             // 00 means you change the uplink interval with the last 2 characters
        // Change uplink interval
        long factor = strtol(payload.substring(6).c_str(), nullptr, 16);
        new_interval = factor * 60000;
        uplink_interval = new_interval;
        return 0;
    }
    if (type == "01") {      // 01 means you Reset the device
        // Reset the device
        dragino_shield.println("ATZ");  //reset LA66
        delay(1000);
        return 1;
    }
}

void read_sensor_values() {
    sensor.listen();
    
    delay(250);
    byte k = read_parameter(inquiry_frame, sizeof(inquiry_frame)); // Error
    delay(250);
    byte n = read_parameter(inquiry_frame, sizeof(inquiry_frame));
    delay(250);
    bat_level = read_battery_level();
}

void print_values() {
    for ( byte value : values ) {
        Serial.print(value, HEX); Serial.print(" ");
    }
    Serial.println();
}

void print_sensor_data() {
    Serial.print("\tAmmonia: "); Serial.print(sensor_data.sensor_values.val1); Serial.println("ppm");
    Serial.print("\tTemperature: "); Serial.print(sensor_data.sensor_values.val2); Serial.println("°C");
    Serial.print("\tHumidity: "); Serial.print(sensor_data.sensor_values.val3); Serial.println("%");
    Serial.print("\tBattery Level: "); Serial.print(bat_level); Serial.println("%");
}

byte read_parameter(byte * parameter, size_t parameter_size) {
    digitalWrite(DE, HIGH);
    digitalWrite(RE, HIGH);
    delay(10);

    if (sensor.write(parameter, parameter_size) == 8) {
        digitalWrite(DE, LOW);
        digitalWrite(RE, LOW);
        for (auto &val : values) {
            val = sensor.read();
            Serial.print(val, HEX); Serial.print(" ");
        }
        Serial.println();
    }
    val1 = (uint16_t)values[14];
    val1 = ((val1 << 8) | values[15]);
    val1 = val1/10;
    sensor_data.sensor_values.val1 = val1;

    val2 = (uint16_t)values[5];
    val2 = ((val2 << 8) | (uint16_t)values[6]);
    val2 = val2/10;
    sensor_data.sensor_values.val2 = val2;

    val3 = (uint16_t)values[3];
    val3 = ((val3 << 8) | (uint16_t)values[4]);
    val3 = val3/10;
    sensor_data.sensor_values.val3 = val3;
    return 0;
}

byte read_battery_level() {
    int raw_value = analogRead(bat_pin);                        // Read raw analog input
    float voltage = raw_value * (5.0/1023.0);                   // Convert to actual voltage
    float bat_level = (((voltage - 2.9)/(4.3 - 2.9)) * 100.0) ;    // Convert to percentage
    // if (voltage <= 3.3) { bat_level = 0.0; }                    // If the voltage level drops below 3.3 make it to zero
    return static_cast<byte>(bat_level);
}
