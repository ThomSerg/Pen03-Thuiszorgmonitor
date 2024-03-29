#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// WIFI //
const char* ssid = "ESP_reciever_1";
const char* password = "123456789";
const char* mqtt_server = "192.168.4.1";

WiFiClient espClient;
PubSubClient client(espClient);

// INPUT/OUTPUT //
const uint8_t digitalPins[3] = {2,3,4}; // three digital outputs to control multiplexer !!!! numbers are wrong !!!!!
const uint8_t input_pin = A0;

// SAMPLE //
const float samplefreq = 50;
const int pres_interval = round(1e6/samplefreq);
unsigned long last_micros = micros();
uint8_t sample_count;

// BUFFERS //
const uint8_t buffer_length = 12;
const uint8_t shifted_buffer_length = 8;
uint16_t pressure_A[buffer_length];
uint16_t pressure_B[buffer_length];
uint16_t pressure_C[buffer_length];
uint16_t pressure_D[buffer_length];
uint16_t *measurement_buffers[4] = {pressure_A, pressure_B, pressure_C, pressure_D};
uint16_t shifted_buffer[shifted_buffer_length];
uint8_t encoded_message[shifted_buffer_length*2];
const char* topic[] = {"pressure_A", "pressure_B", "pressure_C", "pressure_D"};

uint8_t bits_per_encryption = 128;
uint8_t bits_per_measurement = 10;
uint8_t bits_per_int = 16;

void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("outTopic", "hello world");
      // ... and resubscribe
      client.subscribe("inTopic");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(2000);

  // WIFI //
  setup_wifi();
  client.setServer(mqtt_server, 1883);

  // INPUT/OUTPUT //
  pinMode(input_pin, INPUT);
  for (uint8_t i=0; i<3; i++){
    pinMode(digitalPins[i], OUTPUT);
    digitalWrite(digitalPins[i], LOW);
  }
  
}

void loop() {
  // put your main code here, to run repeatedly:

  // WIFI //
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // SAMPLE //
  if (micros() - last_micros >= pres_interval) {
    last_micros += pres_interval;
    /*pinSelect(0);
    pressure_A[sample_count] = analogRead(A0);
    pinSelect(1);
    pressure_B[sample_count] = analogRead(A0);
    pinSelect(2);
    pressure_C[sample_count] = analogRead(A0);
    pinSelect(3);
    pressure_D[sample_count] = analogRead(A0);
    */
    for (uint8_t i=0; i<4; i++){
      pinSelect(i);
      measurement_buffers[i][sample_count] = analogRead(A0);
    }
    sample_count++;
    if (sample_count == buffer_length){
      sample_count = 0;
      for (uint8_t i=0; i<4; i++){
        bitShift(measurement_buffers[i], buffer_length, shifted_buffer, shifted_buffer_length);
        //encryptData(shifted_buffer, );
        splitBuffer(shifted_buffer, shifted_buffer_length, encoded_message);
        client.publish(topic[i], encoded_message, shifted_buffer_length*2); 
      }
    }
  }
}

void pinSelect(uint8_t sensor_pin){   // pin is a number between 0 and 3
  for (uint8_t x=0; x<2; x++){
    byte state = bitRead(sensor_pin, x);    // bitRead reads the bit at position x of the number starting from the right, state can be 0 or 1
    digitalWrite(digitalPins[x], state);    
  }
}

void bitShift(uint16_t *measurement_buffer, uint8_t buffer_length, uint16_t *shifted_buffer, uint8_t shifted_buffer_length) {
  uint8_t current_buffer_index = buffer_length - 2;
  uint8_t current_shifted_buffer_index = shifted_buffer_length - 2;
  //print(measurement_buffer[current_buffer_index])
  shifted_buffer[current_shifted_buffer_index+1] = measurement_buffer[current_buffer_index+1];
  uint16_t current_bits_to_shift = bits_per_int - bits_per_measurement;
  
  while (true) {
    if (current_buffer_index < 0) {
        break;
    }
    
    uint16_t new_element_to_shift = measurement_buffer[current_buffer_index];
    
      
    if (current_bits_to_shift >= bits_per_int) { // als vorige shifted_buffer element volledig leeg is
      shifted_buffer[current_shifted_buffer_index+1] = new_element_to_shift;
      current_bits_to_shift = bits_per_int - bits_per_measurement;
    } else {
      // als er nog ruimte is om op te vullen door te shiften
      uint16_t previous_shifted_buffer_element = shifted_buffer[current_shifted_buffer_index+1];

      // vul vorig shifted_buffer element
      uint16_t bits_to_shift = uint16_t (new_element_to_shift << (bits_per_int - current_bits_to_shift));
      previous_shifted_buffer_element = previous_shifted_buffer_element + bits_to_shift;
      shifted_buffer[current_shifted_buffer_index+1] = previous_shifted_buffer_element;
      
      // plaats overblijvende bits in huidig shifted_buffer element
      if (current_shifted_buffer_index >= 0) {
          uint16_t leftover_bits = new_element_to_shift >> current_bits_to_shift;
          shifted_buffer[current_shifted_buffer_index] = leftover_bits;
          current_bits_to_shift = current_bits_to_shift + (bits_per_int - bits_per_measurement);
          current_shifted_buffer_index --;
      }
    }
    current_buffer_index --;
  
  }
}

void splitBuffer(uint16_t *before_buffer, uint8_t before_buffer_length, uint8_t *after_buffer) {
  for(int i=0; i<before_buffer_length; i++) {
    int element = before_buffer[i];
    uint8_t part_1 = element >> (before_buffer_length/2);
    uint8_t part_2 = (uint16_t(element << (before_buffer_length/2))) >> (before_buffer_length/2);
    after_buffer[2*i] = part_1;
    after_buffer[2*i+1] = part_2;
  }
}
