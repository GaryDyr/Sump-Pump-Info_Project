/*Developed from hirihiro github example for google-home-notifier
  The idea is to trigger a notice to Google Home if the water level of 
  the sump pump gets to a high level because of float or pump glitching. 
  The detection is trivial. Just two separated aluminum
  bars connected to the 3.3 V ESP32 pins and GND. The "sensor" is just 
  a scrap ceiling hanging light monorail which consists of two exposed
  aluminum rails with an integral plastic insulator between them. The plastic 
  was cut between the metal pieces and separated a bit more, because water would
  likely adhere to the plastic even after the water level dropped.
  Sump pit water will have a certain resistance depending on the concentration 
  of dissolved ions, time for quilibration with carbon dioxide in the air, the 
  shape and area of the probes, and the distance between the conductive probes. 
  Conductivity is the reciproval of the resistivity. In principle, we could monitor the 
  analog voltage, while placing the probes in the water at certain heights. From the data
  we could develop a correlation between voltage output, and the water level. In practice, this 
  is not likley to work well. The conductivity of the water itself may vary depending 
  on the flow rate of the water through the soil and its length of stay in the sump pit. In
  a bad rainstorm water may not have time to reach ion equilibration with the soil.

  NOTE IT WAS FOUND TOO NOT BE POSSIBLE TO SUMULTANEOULSY HAVE THE ANALOG AND DIGITAL
  PINS CONNECTED. wITH THE ANALOG PIN CONNECTED THE ESP32 SELF-TRIGGERED IN AN
  INFINITE LOOP, CONSTANTLY WAKING UP.
 
  MAKE SURE WE HAVE THE EXT0 TRIGGER PIN AS A PULLDOWN IN, IOW, THERE SHOULD
  BE A RESISTOR (LIKE 10k) THAT CONNECTS TO GND AND THE GND SIDE OF SENSOR FORK.
  IF USING THE RESISTIVE SENSOR MODULE FROM THE CHEAP RESISTIVE SENSORS,
  THE RESISTOR IS ALREADY ON THE ADAPTER BOARD.
  
  Because the MELIFE board, like other knock offs, ss too wide for a bread board, there are 
  trade-offs to be made. We must have access to: 3.3VDD, also one of the RTC enabled
  GPIO pins 0, 2, 4, 12-15, 25-27 32-39. The MELIFE does have 2, 4, 15 on the right side,
  so can use D2, GPIO02 as digital for water level trigger wake up, and D4 GPIO4 to read 
  analog for testing completeness.
  
  Smtp method of sending emails will not work with gmail, https://support.google.com/accounts/answer/6010255
  Thus, either security has to be downgraded on your primary account (appaerently not just gmail from the way 
  it reads) or set up a second Google account and turn off down the authentication in that account to send emails.
  
  Alternatively, it may be better just to bite bullet, ignore smtp on eSP32 and send emails grom Google Sheets, which 
  does not currently have the problem, because you are already an authorized users.
  */

#include <WiFi.h>
#include <esp8266-google-home-notifier.h>
#include <time.h>
#include <ESP_Mail_Client.h> //only can be used with gmail under special conditions.
#include <WifiClientSecure.h>

//Create notifier object instance
GoogleHomeNotifier ghn;

// ******************INTERNET CONNECTION STUFF*****************************************
const char* ssid     = "Your_SSID";      //"<REPLASE_YOUR_WIFI_SSID>"
const char* password = "Your_Modem/Router Pswd7"; //<REPLASE_YOUR_WIFI_PASSWORD>"
//*******************END INTERNET CONNECTION STUFF*************************************

//********************TIME RELATED VARIABLES*****************************************
//Set repeat interval between alert messages until water level drops;
const long alertInterval = 120; //seconds between announcements, if digital pin low; 20 s for testing; 120 s normal.
// future use bootcount; may be needed with time fcn; store value in RTC memory
// RTC_DATA_ATTR int bootCount = 0; 
unsigned long startTime; //used in testing to check various timing intervals
unsigned long finishTime; // used to hold testing interval time in us
//Variable to store epoch time in RTC memory
RTC_DATA_ATTR unsigned long lastTimeAwake = 0; 
//Variable to hold current epoch time (us)
unsigned long nowTime;
const char* ntpServer = "pool.ntp.org"; 
const long  gmtOffset_sec = -6*3600; //CST time offset
const int   daylightOffset_sec = 3600;
struct tm timeinfo; //output from time.h
int attempts; //used to track connection issues to lan router
int wakePinStatus;
int pinStatus;
float pinState;

time_t now;
//******************END TIME RELATED VARIABLES*******************************************

//******************ESP32 EMAIL STUFF****************************************************
#define SMTP_HOST "smtp.some_org.net"
// use TLS smtp port for gmail
#define SMTP_PORT 587
// GET THE NECESSARY EMAIL CREDENTIALS
/* The sign in credentials */

#define AUTHOR_EMAIL "email_name@some_org.net"
#define AUTHOR_PASSWORD "Some_email_account_pswd"
/* The SMTP Session object used for Email sending */
SMTPSession smtp;
/* Callback function to get the Email sending status */
//this must set some sort of flag to smtp to send response back.
void smtpCallback(SMTP_Status status);

//************GOOGLE SHEETS VARIABLES******************************************************

const int httpsPort = 443; //this is the https port.
WiFiClientSecure gsclient;
//use the Google Script ID, not the Google Sheet ID this was anonymous
const char* host = "https://script.google.com";
String GAS_ID = "ASDFGHJKL;mnbvcxkljhgfdsmnbvcxziuytrewqjhgkjhgfdskjhgfdsmnbvcxz"
//const char* host = "https://script.google.com/macros/s/ASDFGHJKL;mnbvcxkljhgfdsmnbvcxziuytrewqjhgkjhgfdskjhgfdsmnbvcxz/exec";
//For Test Deployment:
//String GAS_ID = "ASDFGHJKL;mnbvcxkljhgfdsmnbvcxziuytrewqjhgkjhgfdskjhgfdsmnbvcxzQ";
//const char* host = "https://script.google.com/macros/s/AKfycbyP-6rqy7tDp9Yb7dDlEfbkwctonauWLc2H06eKa0sV/dev";
//fingerprint not used; using setInsecure
//const char* fingerprint = "46 B2 C3 44 9C 59 09 8B 01 B6 F8 BD 4C FB 00 74 91 2F EF F6";
//************END GOOGLE SHEETS VARIABLES**************************************************

//************ESP32 PIN AND MISCELANIEOUS ASSIGNMENTS**************************************
//Only RTC IO can be used as a source for external wake
//source. They are pins: 0,2,4,12-15,25-27,32-39.
//RTC_DATA_ATTR int bootCount = 0;
//
//ext0 wakeup is quite specific about how pin defined. 
#define GPIO_PIN_WAKEUP GPIO_NUM_2 // digital wake up; GPIO_NUM_X nomenclature from arduino-esp32 library 
#define EXT_WAKEUP_PIN_BITMASK 0x0000  //  was 0x1000 as example =2^12 in hex; not needed for single trigger
//See https://lastminuteengineers.com/esp32-deep-sleep-wakeup-sources/ for how bit mask works for ext1
//ext1 must use one or more of  RTC GPIOs 32-39 pins.
/*set how sensor history is saved and retrieved
  * notice_level:
  1  =     Google Home Mini notification only; default.
  2  = 1 + ESP32 send email
  3  = 1 + Google Sheet send email (SET UP SHEET WITH JS CODE)
  4  = 1 + Google Sheet send mail + store in Firebase with DialogFlow 
*/  
int notice_level = 1;
//***********END ESP32 SPECIFIC ASSIGNMENTS**************************************************

/* Callback function to get Email sending status */
void smtpCallback(SMTP_Status status){
  /* Print the current status */
  Serial.println(status.info());
  /* Print the sending result */
  if (status.success()) {
    Serial.println("----------------");
    Serial.printf("Message sent success: %d\n", status.completedCount());
    Serial.printf("Message sent failed: %d\n", status.failedCount());
    Serial.println("----------------\n");
    struct tm dt;
    for (size_t i = 0; i < smtp.sendingResult.size(); i++) {
      /* Get the result item */
      SMTP_Result result = smtp.sendingResult.getItem(i);
      time_t ts = (time_t)result.timestamp;
      localtime_r(&ts, &dt); //convert Epoch to calendar timep; place in buffer
      Serial.printf("Message No: %d\n", i + 1);
      Serial.printf("Status: %s\n", result.completed ? "success" : "failed");
      Serial.printf("Date/Time: %d/%d/%d %d:%d:%d\n", dt.tm_year + 1900, dt.tm_mon + 1, dt.tm_mday, dt.tm_hour, dt.tm_min, dt.tm_sec);
      Serial.printf("Recipient: %s\n", result.recipients);
      Serial.printf("Subject: %s\n", result.subject);
    }
    Serial.println("----------------\n");
  }
}

//funciton to get time from NTP server pool
void printLocalTime() {
  //this does not get a new NTP time from an NTP server. Instead reads ESP32 RTC time and 
  //configured as desired. What really going on is obscure ( to me). Appears configTime 
  //may only set the time offsets and servers. Just what really gets the world time
  //form the ntp servers is unclear...maybe. https://www.esp8266.com/viewtopic.php?p=89714
  //Once configTime is invoked it seems?? the ESP internally may call out to the time
  //server pool on its own, without user intervention. The time is updated probably hourly or so
  //and placed in some format into the ESP RTC space. We access the time through the RTC,
  //using getLocalTime. GetLocalTime is not true world time, unless near the instant aftre
  //an internal update...I guess. For most stuff the RTC ESP time, is close enough.
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%a, %b %d %Y %H:%M:%S"); //abrev. day name, abrev. mon., day of month, 4 digit year, 24 hrs, 0-60 min, 0-60 s
}

unsigned long getEpochTime() {
  //used to get seconds from  01/01/1970
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return 0;
  }
  //get the epoch time (sec since 1970
  
  time(&now);
  //Serial.println(now);
  return now;
}

//Highly generalized wake up reason function
void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();
  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep. Wake Reason: %d\n",wakeup_reason); break;
  }
}

/* Connect to email server with the session config */
void smtpEmailOut(int pstate) {
  /* 
  Note that the cycle time for sending emails is set in the sendData() function
  Set the callback function to get the sending results 
  */
  smtp.callback(smtpCallback);
  // Declare the session config data
  ESP_Mail_Session session;
  /* Set the session config */
  session.server.host_name = SMTP_HOST;
  session.server.port = SMTP_PORT;
  session.login.email = AUTHOR_EMAIL;
  session.login.password = AUTHOR_PASSWORD;
  //session.login.user_domain = "mydomain.net";
  //The mobizt example has commented out section that indicates pointers used
  //or needed to get at some detailed data, if we need to. Do not?
  //Example at:   https://github.com/mobizt/ESP-Mail-Client/blob/master/examples/Send_Text/Send_Text.ino#L22 
  //suggests many type of options, prune to what we want
  // The Plain text message character set e.g. * The default value is utf-8
  SMTP_Message message;
  message.text.charSet = "utf-8";
  // The content transfer encoding  The default value is "7bit"
  message.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
    /** The message priority
    * esp_mail_smtp_priority_high or 1
    * esp_mail_smtp_priority_normal or 3
    * esp_mail_smtp_priority_low or 5
    * The default value is esp_mail_smtp_priority_low
    */
  message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_normal;
    /** The Delivery Status Notifications e.g.
    * esp_mail_smtp_notify_never
    * esp_mail_smtp_notify_success
    * esp_mail_smtp_notify_failure
    * esp_mail_smtp_notify_delay
    * The default value is esp_mail_smtp_notify_never
    */
  message.response.notify = esp_mail_smtp_notify_success | esp_mail_smtp_notify_failure | esp_mail_smtp_notify_delay;
    /* Set the custom message header */
    //message.addHeader("ESP32 MAILMessage-ID: <somebody#some_org.net>");
    // Set the message headers
  message.sender.name = "ESP32 Sump Pump";
  message.sender.email = "somebody@your_org.net";
  //message.subject = "Sump Pump High Level Alert!";
  message.addRecipient("Somebodys Home", "my_email@some_org.com");
  message.addCc("someone_else@someplace_else.com");
    // Set the message content
    //get time from ESP32 clock
   getLocalTime(&timeinfo); 
  char t_char[32];
  strftime(t_char, sizeof t_char, "%a.%b.%d:%H.%M.%S", &timeinfo); 
  if (pstate == 1) {
    const char *bodyout1 = "; Sump Pump High Level Warning. Check sump pump NOW!!!!!";
    message.subject = "Alert. Sump Pump High Water Level Detected!"; 
    message.text.content =  strcat(t_char, bodyout1); //t_char.c_str() + "; Sump Pump High Level Warning. Check sump pump NOW!!!!!";
  }
  else {
    message.subject = "Sump Pump Level Normal Now";
    const char *bodyout2 = "; Sump Pump water level has returned to normal.";
    message.text.content = strcat(t_char, bodyout2);  
  }
  if (!smtp.connect(&session))
    return;
  /* Start sending Email and close the session */
  if (!MailClient.sendMail(&smtp, &message))
    Serial.println("Error sending Email, " + smtp.errorReason());
}

//send trigger data to google sheet
void sendData(int pump_state) {
  Serial.print("connecting to Google Script: ");
  Serial.println(host);
  String string_pstate = String(pump_state);
  Serial.print("Pump status = ");
  Serial.println(string_pstate);
  //if not setting setInsecure; we have to get fingerprint, which changes regularly, and is unacceptable.
  gsclient.setInsecure();  // weakens security a bit.
  String url = String("/macros/s/") + String(GAS_ID) + String("/exec?status=") + string_pstate;
 //String url = String("/macros/s/") + String(GAS_ID) + String("/dev?status=") + string_pstate; //for testing
  Serial.print("requesting ");
  Serial.println(url);
  if (!gsclient.connect("script.google.com", httpsPort)) {
    Serial.println("Connection to script failed");
    //gsclient.stop();
    return;
  }
 /* don"t use fingerprint, should work without it if using insecure
  if (gsclient.verify(fingerprint, host)) {
    Serial.println("certificate matches");
  } else {
    Serial.println("certificate doesn"t match");
  }
  //GET is used to request data, and uses a simple query string format after the "?"
 */
   gsclient.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + "script.google.com" + "\r\n" +
                 "User-Agent: BuildFailureDetectorESP8266\r\n" + 
                 "Connection: close\r\n\r\n");
  Serial.println("request sent");
  while (gsclient.connected()) {
    String line = gsclient.readStringUntil('\n');
    if (line == "\r") {
      Serial.println("headers received");
      break;
    }
  }
  String line = gsclient.readStringUntil('\n');
  Serial.print("reply was: ");
  Serial.println(line);
  //For whatever reason, initially saw "state", but later code was 287
  //According to HTTP registry codesin range 2xx are success.
    if (line.startsWith("{\"state\":\"success\"") || (line.startsWith("2"))) {
    Serial.println("Data transfer successfull!");
  } else {
    Serial.println("Data transfer failed");
  }
  Serial.println("closing connection");
  gsclient.println(" Connection: close"); 
  gsclient.stop();
}

void FireData(int status) {}
  //place holder only; to store the event high and low in Google Firebase. Obviously, not available


//When the Wakeup pin is triggered LOW, the ESP32 wakes up and runs 
//setup
void setup() {
  //esp is awake because setup started: Either because of a reboot, or D2 going High indicating voltage at pin;
  unsigned long tdiff;
  // if we need to store the time, this is how:
  //RTC_DATA_ATTR time_t now;
  //RTC_DATA_ATTR uint64_t Mics = 0;
  //RTC_DATA_ATTR struct tm * timeinfo;
  
  //declare ext0 trigger pin, so we can read state later
  pinMode(GPIO_PIN_WAKEUP, INPUT);
  //Deal with LED pins
  //MELIFE ESP32 HAS POWER LED, BUT NOT 2ND LED
  //pinMode(LED_BUILTIN, OUTPUT); // not sure this works anyway.
  //digitalWrite(LED_BUILTIN, HIGH); // 
  //digitalWrite(LED_PIN2, LOW);
  Serial.begin(115200);
  Serial.print("connecting to WiFi");
  attempts = 0;
  WiFi.mode(WIFI_STA);
  startTime = micros(); 
  WiFi.begin(ssid, password);
  
  //Connect to router/lan
  //allow up to 10 attempts to connect  @ 250ms = 5s to get connected.
  while ((WiFi.status() != WL_CONNECTED) && (attempts <= 10)) {
    delay(500); //may be less, but play it safe
    Serial.print(".");
    ++attempts;
    //Connection not established in 5s, put to sleep and hope next cycle works.  
    if (attempts == 15) {
      Serial.println("Failed to connect to router. Going back to sleep.");
      digitalWrite(GPIO_PIN_WAKEUP, HIGH);
      esp_sleep_enable_ext0_wakeup(GPIO_PIN_WAKEUP,0); //1 = High, 0 = Low numbers can be 
      //next should bypass rest of code
      esp_deep_sleep_start();
      break;
    }
  }
  //get time to check how long it took to connect to router
  finishTime = micros() - startTime;
  Serial.println("");
  Serial.print("I am awake. Connected with IP address: ");
  Serial.println(WiFi.localIP());  //Print the local IP
  Serial.print("; time to connect (us) was: ");
  Serial.print(finishTime);
  Serial.print(", in ");
  Serial.print(attempts);
  Serial.println(" attempts.");
  //found that for my lan took less than 1 s with at most 2 attemps to connect, but that is with 500 ms delay. 
  
  //get NTP time and how long to get time, it is the basis of how often we can send
  //emails
  startTime = micros();
  //init and fill ESP RTC memory with the latest NTP time from the server pool
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer); 
  //check and get the time from the RTC
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain NTP time");
    //So do we just continue? We don"t need the time to continue as alert. 
    //return; 
  }  
  finishTime = micros() - startTime;
  Serial.print("NTP access Time, us: ");
  Serial.println(finishTime);
  printLocalTime(); //prints last NTP time that was stored.
  //the config and getLocalTime process too a max of 150 ms, although average is about 70 ms.
  
  //get current Epoch time to compare with the lastTimeAwake in RTC memory
  nowTime = getEpochTime(); //access the last local time configured in RTC, i.e, now()
  if (nowTime != 0) {
     Serial.print("epoch time stored in RTC: ");
    Serial.println(lastTimeAwake);
  }
  
  //the LM393 comparator module of moisture sensor is LOW on D0, when the voltage
  //is high, i.e. water on probe, and normally HIGH when the voltage (no water) across leads.
  //The LM393 module has two LEDS; power and signal level. Adjust the variable
  //resistor, (sensitivity) until the signal LED just goes off.
  //ESP32 sleeps until pin D2 goes low. which is high level detected;
  //Though RTC clock will loose time accuracy, timy amount is insignificant here.
 
 //Print the wakeup reason for ESP32 
  print_wakeup_reason();
  // Read the state of the pin.
  // It is possible that a transent LOW could trigger the ESP32. Do several pin reads to
  //determine whether the pin is staying HIGH; if HIGH go bck to sleep, and do nothing.
  pinState = 0.0;
  for (int i=0; i < 5; i++) {
    pinStatus = digitalRead(GPIO_PIN_WAKEUP);
    if (pinStatus) {
      pinState += 1.0;
      delay(200); //we delay another read for 200 ms
    }          
  }
  //judgement call; if the average pin status is above 0.60, i.e., in 1 s 3/5 reads were above 1
  //this a false positive; just go back to sleep.   
  pinState = pinState/5.0; 
  if (pinState >= 0.60) {
    //go back to sleep; transient    
    Serial.println("Digital pin: HIGH...low water level");
    Serial.println("False trigger. Going back to sleep.");
    digitalWrite(GPIO_PIN_WAKEUP, HIGH);
    esp_sleep_enable_ext0_wakeup(GPIO_PIN_WAKEUP,0); //1 = High, 0 = Low numbers can be   
    delay(1000);
    esp_deep_sleep_start();
  }
  else {
    //valid high water alert
    Serial.println("Digital pin: LOW...high water level.");
    wakePinStatus = 1;
  }  
  Serial.print("pin status is: ");
  Serial.println(wakePinStatus); 
  delay(1000); //time to display stuff
  
  //Send a status=1 whenever setup is triggered, but not on every voice alert sent.
  //this will include notices on start up.
   //If voltage, i.e., water across leads, the LM393 comparater goes LOW; begin monitor
  //and call at (alert_interval x 1000) until digital pin goes high again.
  //NOTE ACTIONS IS BEING REVERSED, SO THAT "1" = ALERT AND "0" = NORMAL.  
  //On startup a value will be passed, and on the second cycle will be updated.
  switch (notice_level) {
    case 1:
      sendData(1);
      break;
    case 2:
      //Using ESP32 to send email; we are comparing seconds here.
      //send only six emails/ 24 hours so if time is greater than 3600*4 = 14400 s 
      //First cycle will print this as well, then  bypass while and print and send 0
      sendData(1);
      if (nowTime - lastTimeAwake > 14400) {
        smtpEmailOut(1);
         }
      break;
    case 3: 
      sendData(1);
      break;
    case 4:
      FireData(1);
      break;
  }
      
  //Start internal watch loop; continue looping uwhile water level is high (pin LOW).
  //loop cycles at alertInterval seconds to say a Google Home Mini alert text
  //3 s delay to allow it to complete the message, which adds on to the 
  //much longer audio alert message.
  //There is no way out of this loop, other than reseting ESP
   const char displayName[] = "Family Room speaker";  
  while (digitalRead(GPIO_PIN_WAKEUP) == LOW) {
    //SPEAK UP!
    //WiFi.MODE(WIFI_STA); //UNCOMMENT IF DECIDE TO TURN WIFI OFF AND ON.
    if (ghn.device(displayName, "en") != true) {
      Serial.print("error finding speaker: ");
      Serial.println(ghn.getLastError());
    }
    else {
      Serial.print("found Google Home(");
      Serial.print(ghn.getIPAddress());
      Serial.print(":");
      Serial.print(ghn.getPort());
      Serial.println(")");
      if (ghn.notify("Alert! Sump Pump high level reached!") != true) {
        Serial.println(ghn.getLastError());
        delay(3000); //delay for sending notice 
      }
    }
    delay(alertInterval*1000);  //convert seconds to millisec; delay and loop back to check pin again.
  } //end while LOW
  
  /*
  Alway send a status=0 msg to Sheets when out of while voice alert loop.
  Digital pin is now HIGH, i.e, water level below sensor fork
  send notice to Google Sheets; then go to sleep now; wake on external trigger only.
  ext0 uses RTC_IO to wakeup, thus requires RTC peripherals
  to be on while ext1 uses RTC Controller, so doesnt need
  peripherals to be powered on.
  Note that using internal pullups/pulldowns also requires
  RTC peripherals to be turned on.
  */
  Serial.println(" ");
  Serial.println("Pin registering HIGH. All good. Going to sleep now.");
  if (ghn.device(displayName, "en") != true) {
      Serial.print("error finding speaker: ");
      Serial.println(ghn.getLastError());
  }
  else {
    Serial.print("found Google Home(");
    Serial.print(ghn.getIPAddress());
    Serial.print(":");
    Serial.print(ghn.getPort());
    Serial.println(")");
    if (ghn.notify("Update! Sump Pump now normal.") != true) {
      Serial.println(ghn.getLastError());
      delay(3000); //delay for sending notice 
    }
  }
  switch (notice_level) {
    case 1:
      sendData(0);
      break;
    case 2:
      sendData(0);
      //we always send a all clear message, could exceed 100/day if ESP32 goes nuts.
      smtpEmailOut(0);
      break;
    case 3: 
      sendData(0);
      break;
    case 4:
      FireData(0);
      break;
  }
  //Store the current epoch time in RTC memory; this works because waking from sleep, not from cold start
  if (nowTime >= 0) {
    lastTimeAwake = getEpochTime();
  }
  //configure the wake up source
  digitalWrite(GPIO_PIN_WAKEUP, HIGH); //NOT SURE THIS IS NEEDED, BUT DID NOT HURT
  delay(20);
  esp_sleep_enable_ext0_wakeup(GPIO_PIN_WAKEUP,0); //0 = LOW pin; WAKE UP..WATER HIGH 
  //If you were to use ext1, you would use it like...
  //esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK,ESP_EXT1_WAKEUP_ANY_HIGH);
  delay(1000);
  esp_deep_sleep_start();
  Serial.println("This line will never be printed...hopefully");
} //close setup

void loop() {
  // No code here, trigger externally
  }
