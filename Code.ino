#include <dmx.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "soc/soc.h"  //necessary for dmx.h (fix)
#include "soc/rtc_cntl_reg.h"  //necessary for dmx.h (fix)
#include "esp_wifi.h"  //necessary for dmx.h (fix)

#define ENC_TYPE_NONE 7
#define MAX_INPUT 50

byte bySetupStep = 0;
int iChannel = 1;   //first channel of DMX communication
String sSsid,sPassword,sMessage;
String sColorName;
char input_line[MAX_INPUT];
unsigned int input_pos = 0;
bool bCommandRead = 0;
unsigned long lLastTime = 0;
bool bISRHappened = 0;
byte byMode = 0, byColor=1;
byte byModeServer;
int iTempMode, iServerTemp;
bool bMenu = 0, bSubmenu = 0;
bool bServer=0;
bool bSender=0;
bool bDegubMess = 1;  //enables debug messages, for disable = 0
//variables for DMX effects
bool bRedEnable=0, bGreenEnable=0, bBlueEnable=1;
bool bRedDirection=1, bGreenDirection=0, bBlueDirection=1;
byte byRed=0, byGreen=255, byBlue=250;
byte byStrobo=0, byPride = 0;

hw_timer_t *Timer1_Cfg = NULL;

WebServer DMX_Server(80);
//-------------- Setup --------------
void setup()
  {
  Serial.begin(115200);
  Serial.println("SERIAL ON");
  Serial.println("DMX 512 Light Show Driver");
  Serial.println("Made on CVUT in Prague");
  FirstScreen();
  }

//-------------- Prvni obrazovka (CLI) --------------
void FirstScreen()
{
  Serial.println("Would you like to connect to a network? [Y - Yes/N - No]\n*Please keep in mind that everybody on the local network will be able to connect to this device via IP address.*");
  bySetupStep = 1;
  bool bStatus=0;
  input_pos = 0;
  while(1)
  {
    while(Serial.available() > 0)
    {
      processIncomingByte(Serial.read (),0);
      bStatus = 1;
    }
    if((Serial.available()==0)&&(bStatus==1))
    {
      if(bySetupStep==4)
      {
        Serial.println("Done config, exiting to Loop.");
        LoadDefaults();
      }
      break;
    }
  }
}

//-------------- Cteni z prikazove radky (terminal) - setup --------------
void processIncomingByte(byte inByte, int iFunction)
  {
    //Serial.println(inByte);
    switch(inByte)
    {
      case '\n':
        input_line [input_pos] = 0;
        if(iFunction==1)
        {
          sPassword = String(input_line);
          input_pos = 0;
          for(int i = 0; i< MAX_INPUT; i++)
          {
            input_line[i]='\0';
          }
        }
        else
        {
          input_pos = 0;
          process_data(input_line);
        }
        input_pos = 0;  
      break;
      case '\r':
      break;
      case 0xFF:
      break;
      default:
        if (input_pos < (MAX_INPUT - 1))
          input_line[input_pos++] = inByte;
        if ((Serial.available()==0)&&(input_pos>0))
        {
          if(iFunction == 1)
          {
            Serial.flush();
            sPassword = String(input_line);
          }
          else
          {
            Serial.flush();
            input_pos = 0;
            process_data(input_line);
          }
        }
      break;
    }
  }

//-------------- Vyhodnoceni nactenych znaku pro spusteni modu CLI/server --------------
void process_data(const char * data)
  {
    Serial.flush();
    //Serial.println(data);
    switch(*data)
    {
      case 'Y' :
      if(bySetupStep == 1)
      {
        Serial.println("");
        WiFi_connect();
      }
      break;
      case 'N' :
      bySetupStep = 3;
      Serial.println("\nInitializing CLI mode.");
      InitDmx();
      break;
      default:
      Serial.print("Received invalid response: ");
      Serial.print(data);
      Serial.print(".");
      for(int i = 0; i< MAX_INPUT; i++)
          {
            input_line[i]='\0';
          }
          FirstScreen();
      break;
    }
  }
//-------------- Pripojeni k siti --------------
void WiFi_connect()
  {
    if(bySetupStep==1)
    {
      bySetupStep = 2;
    }
    //-------------- Skenovani siti --------------
    Serial.println("Scanning available networks, please wait.");
    uint32_t brown_reg_temp = READ_PERI_REG(RTC_CNTL_BROWN_OUT_REG);
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // disable brownout detection
    WiFi.setHostname("ESP32-DMX512");
    WiFi.mode(WIFI_STA);
    esp_wifi_set_ps(WIFI_PS_NONE);  //https://forum.mikrotik.com/viewtopic.php?t=139511 - cannot connect to Mikrotik
    int n = WiFi.scanNetworks();
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, brown_reg_temp); // enable brownout detection
    Serial.print("Scan done, ");
    if (n == 0)
    {
      Serial.println("no networks found.");
      FirstScreen();
    }
    else
    {
      Serial.print(n);
      Serial.println(" networks found.");
      for (int i = 0; i < n; ++i)
      {
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.print(WiFi.SSID(i));
        Serial.print(" (");
        Serial.print(WiFi.RSSI(i));
        Serial.print(")");
        Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? "" : "*");
        delay(10);
      }
    }
    //-------------- Zadani pozadovane site (cteni uzivatelskeho vstupu) --------------
    int iSsid = 0;
    Serial.println("Please enter WiFi number in table:");
    for(;iSsid==0;)
    {
      while(Serial.available () > 0)
      {
        iSsid = Serial.parseInt();
      }
    }
    Serial.print("\nEntered SSID number: ");
    Serial.println(iSsid);
    Serial.print("That means network: ");
    Serial.println(WiFi.SSID(iSsid-1));
    if(WiFi.encryptionType(iSsid-1) == ENC_TYPE_NONE)
    {
      Serial.println("No password is required, WiFi is non-protected.");
    }
    else
    {
      Serial.println("Please enter WiFi Password: (there is an 600ms timeout as ENTER key for Hercules users)");
      bool bStatus = 0;
      long lMillis;
      while(1)
      {
        while(Serial.available() > 0)
        {
          processIncomingByte(Serial.read(),1);
          bStatus = 1;
          lMillis = millis();
        }
        if((Serial.available()==0)&&(bStatus==1)&&((lMillis+600)<millis())) //end of reading - no serial data available and no new data were received in last 300ms
        {
          //Serial.println("End of reading.");
          for(int i = 0; i< MAX_INPUT; i++)
          {
            input_line[i]='\0';
          }
          input_pos = 0;
          break;
        }
      }
      Serial.print("\nEntered password: ");
      Serial.println(sPassword);
    }
    //-------------- Pokus o pripojeni --------------
    Serial.print("\nConnecting to: ");
    Serial.println(WiFi.SSID(iSsid-1));
    brown_reg_temp = READ_PERI_REG(RTC_CNTL_BROWN_OUT_REG);
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // disable brownout detection
    for(int i = 0; ((i<10)&&(WiFi.status() != WL_CONNECTED));i++)
    {
      if(WiFi.encryptionType(iSsid-1) == ENC_TYPE_NONE)
      {
        if(i == 0) Serial.println("Open WiFi! Your connection might be hijacked!");
        WiFi.begin(WiFi.SSID(iSsid-1));
      }
      else
      {
        WiFi.begin(WiFi.SSID(iSsid-1), sPassword);
      }
      Serial.print(".");
      delay(2000);
    }
    Serial.println("");
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, brown_reg_temp); // enable brownout detection
    if(WiFi.status() == WL_CONNECTED)
    {
      Serial.println("Connection success. Now, you can connect via network.");
      Serial.print("Controller IP address: ");
      Serial.println(WiFi.localIP());
      Serial.print("Controller hostname: ");
      Serial.println(WiFi.getHostname());
      ServerStart();
      bServer=1;
      if(bySetupStep==2)bySetupStep=3;
      //WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // disable brownout detection
      if(bySetupStep==3)InitDmx();  //if in startup, init DMX Universe
    }
    else
    {
      Serial.println("Sorry but network is unreachable. Consider using console mode (CLI) only or try again.\n");
      WiFi.disconnect();  //For safety reasons, calling disconnect function
      WiFi.mode(WIFI_OFF);  //Turning WiFi off
      if(bySetupStep!=4)
      {
        FirstScreen();  //Return CLI/WiFi menu if in startup
      }
    }
  }
//-------------- Start ESP32 webserveru --------------
void ServerStart()
{
  Serial.println("Starting server! (This may took a while.)");
  byModeServer = byMode;
  DMX_Server.on("/", handle_OnConnect);
  //Dynamic effects:
  DMX_Server.on("/0", []() {iTempMode=0; bSender = 1; ChangeMode(&iTempMode); byModeServer=0; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/1", []() {iTempMode=1; bSender = 1; ChangeMode(&iTempMode); byModeServer=1; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/2", []() {iTempMode=2; bSender = 1; ChangeMode(&iTempMode); byModeServer=2; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  //Static colors:
  DMX_Server.on("/10", []() {iTempMode=3; bSender = 1; iServerTemp=1; ChangeMode(&iTempMode); byModeServer=10; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/11", []() {iTempMode=3; bSender = 1; iServerTemp=2; ChangeMode(&iTempMode); byModeServer=11; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/12", []() {iTempMode=3; bSender = 1; iServerTemp=3; ChangeMode(&iTempMode); byModeServer=12; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/13", []() {iTempMode=3; bSender = 1; iServerTemp=4; ChangeMode(&iTempMode); byModeServer=13; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/14", []() {iTempMode=3; bSender = 1; iServerTemp=5; ChangeMode(&iTempMode); byModeServer=14; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/15", []() {iTempMode=3; bSender = 1; iServerTemp=6; ChangeMode(&iTempMode); byModeServer=15; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/16", []() {iTempMode=3; bSender = 1; iServerTemp=7; ChangeMode(&iTempMode); byModeServer=16; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/17", []() {iTempMode=3; bSender = 1; iServerTemp=8; ChangeMode(&iTempMode); byModeServer=17; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/18", []() {iTempMode=3; bSender = 1; iServerTemp=9; ChangeMode(&iTempMode); byModeServer=18; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/19", []() {iTempMode=3; bSender = 1; iServerTemp=10; ChangeMode(&iTempMode); byModeServer=19; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/20", []() {iTempMode=3; bSender = 1; iServerTemp=11; ChangeMode(&iTempMode); byModeServer=20; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/21", []() {iTempMode=3; bSender = 1; iServerTemp=12; ChangeMode(&iTempMode); byModeServer=21; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/22", []() {iTempMode=3; bSender = 1; iServerTemp=13; ChangeMode(&iTempMode); byModeServer=22; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/23", []() {iTempMode=3; bSender = 1; iServerTemp=14; ChangeMode(&iTempMode); byModeServer=23; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/24", []() {iTempMode=3; bSender = 1; iServerTemp=15; ChangeMode(&iTempMode); byModeServer=24; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/25", []() {iTempMode=3; bSender = 1; iServerTemp=16; ChangeMode(&iTempMode); byModeServer=25; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/26", []() {iTempMode=3; bSender = 1; iServerTemp=17; ChangeMode(&iTempMode); byModeServer=26; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/27", []() {iTempMode=3; bSender = 1; iServerTemp=18; ChangeMode(&iTempMode); byModeServer=27; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/28", []() {iTempMode=3; bSender = 1; iServerTemp=19; ChangeMode(&iTempMode); byModeServer=28; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/29", []() {iTempMode=3; bSender = 1; iServerTemp=20; ChangeMode(&iTempMode); byModeServer=29; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/30", []() {iTempMode=3; bSender = 1; iServerTemp=21; ChangeMode(&iTempMode); byModeServer=30; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/31", []() {iTempMode=3; bSender = 1; iServerTemp=22; ChangeMode(&iTempMode); byModeServer=31; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/32", []() {iTempMode=3; bSender = 1; iServerTemp=23; ChangeMode(&iTempMode); byModeServer=32; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/33", []() {iTempMode=3; bSender = 1; iServerTemp=24; ChangeMode(&iTempMode); byModeServer=33; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  //Strobo effects:
  DMX_Server.on("/40", []() {iTempMode=4; bSender = 1; iServerTemp=20; ChangeMode(&iTempMode); byModeServer=40; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/41", []() {iTempMode=4; bSender = 1; iServerTemp=24; ChangeMode(&iTempMode); byModeServer=41; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/42", []() {iTempMode=4; bSender = 1; iServerTemp=28; ChangeMode(&iTempMode); byModeServer=42; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/50", []() {iTempMode=4; bSender = 1; iServerTemp=10; ChangeMode(&iTempMode); byModeServer=50; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/51", []() {iTempMode=4; bSender = 1; iServerTemp=14; ChangeMode(&iTempMode); byModeServer=51; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.on("/52", []() {iTempMode=4; bSender = 1; iServerTemp=18; ChangeMode(&iTempMode); byModeServer=52; DMX_Server.send(200, "text/html", SendHTML(byModeServer));});
  DMX_Server.onNotFound(handle_NotFound);
  DMX_Server.begin();
  Serial.println("Server has successfully started.");
  bServer = 1;
}
//-------------- Server source code --------------
String SendHTML(byte byMode) {
  String ptr = "<!DOCTYPE html>";
  ptr += "<html>";
  ptr += "<head>";
  ptr += "<title>ESP32 DMX512 Controller</title>";
  ptr += "<meta name='viewport' content='width=device-width, initial-scale=1.0'; charset=utf-8>";
  ptr += "<link href='https://fonts.googleapis.com/css?family=Open+Sans:300,400,600' rel='stylesheet'>";
  ptr += "</head>";
  ptr += "<body style='background-color: rgb(210,210,210);'>";
  //ptr += "<h1 st>ESP32 DMX512 Controller</h1>";
  ptr += "<pre style='text-align:center; font-size:16px; margin-left:auto; margin-right:auto;'> ____ ____ ____ ____ ____ _________ ____ ____ ____ ____ ____ ____ _________ ____ ____ ____ ____ ____ ____ ____ ____ ____ ____ \n";
  ptr +="||E |||S |||P |||3 |||2 |||       |||D |||M |||X |||5 |||1 |||2 |||       |||C |||O |||N |||T |||R |||O |||L |||L |||E |||R ||\n";
  ptr +="||__|||__|||__|||__|||__|||_______|||__|||__|||__|||__|||__|||__|||_______|||__|||__|||__|||__|||__|||__|||__|||__|||__|||__||\n";
  ptr +="|/__\\|/__\\|/__\\|/__\\|/__\\|/_______\\|/__\\|/__\\|/__\\|/__\\|/__\\|/__\\|/_______\\|/__\\|/__\\|/__\\|/__\\|/__\\|/__\\|/__\\|/__\\|/__\\|/__\\|\n</pre>";
  ptr += "<h1 style='margin-bottom:2px;color: brown; font-size:25px; text-align: center; font-family: Courier New;'>Made at CVUT FEL in Prague</h1>";
  ptr += "<hr style='margin-top:3px; margin-bottom:3px; background-color: rgb(100,0,77); color: rgb(100,0,77); height:5px'>";
  ptr += "<h2 style='margin-bottom:5px; margin-top: 0px; color: olive; font-size:22px; text-align: center; font-family: system-ui;'>Dynamic effects:</h2>";
  ptr += "<table style='text-align: center;margin-right: auto; margin-left:auto; width: 100%'>";
  ptr += "<tr><td><a href=\'/0\'><button style='margin-top:auto;margin-bottom:auto;height:50px;width:400px;background-color:rgb(64,64,64);font-size:35px;font-family:Open Sans;color: ";
  ptr += (byMode == 0) ? "rgb(30,210,0);'" : "#000;'";
  ptr += "><b>COLOR CYCLE</b></button></a></td><td><a href=\'/1\'><button style='margin-top:auto;margin-bottom:auto;height:50px;width:400px;background-color:rgb(64,64,64);font-size:35px;font-family:Open Sans;color: ";
  ptr += (byMode == 1) ? "rgb(30,210,0);'" : "#000;'";
  ptr += "><b>PRIDE EFFECT</b></button></a></td><td><a href=\'/2\'><button style='margin-top:auto;margin-bottom:auto;height:50px;width:400px;background-color:rgb(64,64,64);font-size:35px;font-family:Open Sans;color: ";
  ptr += (byMode == 2) ? "rgb(30,210,0);'" : "#000;'";
  ptr += "><b>RAINBOW</b></button></a></td></tr>";
  ptr += "</table>";
  ptr += "<hr style='margin-top:15px; margin-bottom:3px; background-color: rgb(100,0,77); color: rgb(100,0,77); height:5px'>";
  ptr += "<h2 style='margin-bottom:5px; margin-top: 0px; color: olive; font-size:22px; text-align: center; font-family: system-ui;'>Color pallette:</h2>";
  ptr += "<table style='text-align: center;margin-right: auto; margin-left:auto; width: 100%'>";
  ptr += "<tr><td><a href=\'/10\'><button style='margin-top: auto;height: 50px;width: 300px;background-color: rgb(255,0,0); font-size: 35px; font-family: Open Sans; color: ";  //Red
  ptr += (byMode == 10) ? "#FFFFFF;'" : "rgb(255,0,0);'";
  ptr += "><b>ACTIVATED</b></button></a></td><td><a href=\'/11\'><button style='margin-top: auto;margin-bottom:auto;height: 50px;width: 300px;background-color: rgb(15,190,15); font-size: 35px; font-family: Open Sans; color: "; //Green
  ptr += (byMode == 11) ? "#FFFFFF;'" : "rgb(15,190,15);'";
  ptr += "><b>ACTIVATED</b></button></a></td><td><a href=\'/12\'><button style='margin-top: auto;margin-bottom:auto;height: 50px;width: 300px;background-color: rgb(0,0,255); font-size: 35px; font-family: Open Sans; color: "; //Blue
  ptr += (byMode == 12) ? "#FFFFFF;'" : "rgb(0,0,255);'";
  ptr += "><b>ACTIVATED</b></button></a></td><td><a href=\'/13\'><button style='margin-top: auto;margin-bottom:auto;height: 50px;width: 300px;background-color: rgb(255,255,255); font-size: 35px; font-family: Open Sans; color: "; //White
  ptr += (byMode == 13) ? "#000;'" : "rgb(255,255,255);'";
  ptr += "><b>ACTIVATED</b></button></a></td></tr>";
  ptr += "<tr><td><a href=\'/14\'><button style='margin-top: auto;margin-bottom:auto;height: 50px;width: 300px;background-color: rgb(255,165,30); font-size: 35px; font-family: Open Sans; color: "; //Orange
  ptr += (byMode == 14) ? "#000;'" : "rgb(255,165,30);'";
  ptr += "><b>ACTIVATED</b></button></a></td><td><a href=\'/15\'><button style='margin-top: auto;margin-bottom:auto;height: 50px;width: 300px;background-color: rgb(220,115,0); font-size: 35px; font-family: Open Sans; color: "; //Darkorange
  ptr += (byMode == 15) ? "#000;'" : "rgb(220,115,0);'";
  ptr += "><b>ACTIVATED</b></button></a></td><td><a href=\'/16\'><button style='margin-top: auto;margin-bottom:auto;height: 50px;width: 300px;background-color: rgb(255,175,145); font-size: 35px; font-family: Open Sans; color: "; //Salmon
  ptr += (byMode == 16) ? "#000;'" : "rgb(255,175,145);'";
  ptr += "><b>ACTIVATED</b></button></a></td><td><a href=\'/17\'><button style='margin-top: auto;margin-bottom:auto;height: 50px;width: 300px;background-color: rgb(240,220,180); font-size: 35px; font-family: Open Sans; color: "; //Cream
  ptr += (byMode == 17) ? "#000;'" : "rgb(240,220,180);'";
  ptr += "><b>ACTIVATED</b></button></a></td></tr>";
  ptr += "<tr><td><a href=\'/18\'><button style='margin-top: auto;height: 50px;width: 300px;background-color: rgb(255,255,120); font-size: 35px; font-family: Open Sans; color: ";  //Yellow
  ptr += (byMode == 18) ? "#000;'" : "rgb(255,255,120);'";
  ptr += "><b>ACTIVATED</b></button></a></td><td><a href=\'/19\'><button style='margin-top: auto;margin-bottom:auto;height: 50px;width: 300px;background-color: rgb(255,230,185); font-size: 35px; font-family: Open Sans; color: "; //Ivory
  ptr += (byMode == 19) ? "#000;'" : "rgb(255,230,185);'";
  ptr += "><b>ACTIVATED</b></button></a></td><td><a href=\'/20\'><button style='margin-top: auto;margin-bottom:auto;height: 50px;width: 300px;background-color: rgb(175,255,140); font-size: 35px; font-family: Open Sans; color: "; //Vest
  ptr += (byMode == 20) ? "#000;'" : "rgb(175,255,140);'";
  ptr += "><b>ACTIVATED</b></button></a></td><td><a href=\'/21\'><button style='margin-top: auto;margin-bottom:auto;height: 50px;width: 300px;background-color: rgb(160,240,35); font-size: 35px; font-family: Open Sans; color: "; //Lime
  ptr += (byMode == 21) ? "#000;'" : "rgb(160,240,35);'";
  ptr += "><b>ACTIVATED</b></button></a></td></tr>";
  ptr += "<tr><td><a href=\'/22\'><button style='margin-top:auto;margin-bottom:auto;height:50px;width:300px;background-color:rgb(140,255,160);font-size:35px;font-family:Open Sans;color: ";  //Seagreen
  ptr += (byMode == 22) ? "#000;'" : "rgb(140,255,160);'";
  ptr += "><b>ACTIVATED</b></button></a></td><td><a href=\'/23\'><button style='margin-top:auto;margin-bottom:auto;height:50px;width:300px;background-color:rgb(15,255,140);font-size: 35px;font-family:Open Sans;color: "; //Teal
  ptr += (byMode == 23) ? "#000;'" : "rgb(15,255,140);'";
  ptr += "><b>ACTIVATED</b></button></a></td><td><a href=\'/24\'><button style='margin-top:auto;margin-bottom:auto;height:50px;width:300px;background-color:rgb(145,215,255);font-size: 35px;font-family:Open Sans;color: "; //Mint
  ptr += (byMode == 24) ? "#000;'" : "rgb(145,215,255);'";
  ptr += "><b>ACTIVATED</b></button></a></td><td><a href=\'/25\'><button style='margin-top:auto;margin-bottom:auto;height:50px;width:300px;background-color:rgb(0,155,240);font-size:35px;font-family:Open Sans;color: "; //Caribbean
  ptr += (byMode == 25) ? "#000;'" : "rgb(0,155,240);'";
  ptr += "><b>ACTIVATED</b></button></a></td></tr>";
  ptr += "<tr><td><a href=\'/26\'><button style='margin-top:auto;margin-bottom:auto;height:50px;width:300px;background-color:rgb(10,100,255);font-size:35px;font-family:Open Sans;color: ";  //Slate
  ptr += (byMode == 26) ? "#000;'" : "rgb(10,100,255);'";
  ptr += "><b>ACTIVATED</b></button></a></td><td><a href=\'/27\'><button style='margin-top:auto;margin-bottom:auto;height:50px;width:300px;background-color:rgb(65,65,255);font-size: 35px;font-family:Open Sans;color: "; //Pacific
  ptr += (byMode == 27) ? "#000;'" : "rgb(65,65,255);'";
  ptr += "><b>ACTIVATED</b></button></a></td><td><a href=\'/28\'><button style='margin-top:auto;margin-bottom:auto;height:50px;width:300px;background-color:rgb(130,40,255);font-size: 35px;font-family:Open Sans;color: "; //Violet
  ptr += (byMode == 28) ? "#000;'" : "rgb(130,40,255);'";
  ptr += "><b>ACTIVATED</b></button></a></td><td><a href=\'/29\'><button style='margin-top:auto;margin-bottom:auto;height:50px;width:300px;background-color:rgb(160,235,150);font-size:35px;font-family:Open Sans;color: "; //Lightgreen
  ptr += (byMode == 29) ? "#000;'" : "rgb(160,235,150);'";
  ptr += "><b>ACTIVATED</b></button></a></td></tr>";
  ptr += "<tr><td><a href=\'/30\'><button style='margin-top:auto;margin-bottom:auto;height:50px;width:300px;background-color:rgb(160,70,255);font-size:35px;font-family:Open Sans;color: ";  //Purple
  ptr += (byMode == 30) ? "#000;'" : "rgb(160,70,255);'";
  ptr += "><b>ACTIVATED</b></button></a></td><td><a href=\'/31\'><button style='margin-top:auto;margin-bottom:auto;height:50px;width:300px;background-color:rgb(200,30,220);font-size: 35px;font-family:Open Sans;color: "; //Barbie
  ptr += (byMode == 31) ? "#000;'" : "rgb(200,30,220);'";
  ptr += "><b>ACTIVATED</b></button></a></td><td><a href=\'/32\'><button style='margin-top:auto;margin-bottom:auto;height:50px;width:300px;background-color:rgb(205,65,100);font-size: 35px;font-family:Open Sans;color: "; //Broadway
  ptr += (byMode == 32) ? "#000;'" : "rgb(205,65,100);'";
  ptr += "><b>ACTIVATED</b></button></a></td><td><a href=\'/33\'><button style='margin-top:auto;margin-bottom:auto;height:50px;width:300px;background-color:rgb(955,40,90);font-size:35px;font-family:Open Sans;color: "; //Firepink
  ptr += (byMode == 33) ? "#000;'" : "rgb(255,40,90);'";
  ptr += "><b>ACTIVATED</b></button></a></td></tr>";
  ptr += "</table>";
  ptr += "<hr style='margin-top:3px; margin-bottom:3px; background-color: rgb(100,0,77); color: rgb(100,0,77); height:5px'>";
  ptr += "<h2 style='margin-bottom:5px; margin-top: 0px; color: olive; font-size:22px; text-align: center; font-family: system-ui;'>Strobo effects:</h2>";
  ptr += "<table style='text-align: center;margin-right: auto; margin-left:auto; width: 100%'>";
  ptr += "<tr><td><a href=\'/40\'><button style='margin-top:auto;margin-bottom:auto;height:50px;width:400px;background-color:rgb(255,255,180);font-size:35px;font-family:Open Sans;color: ";
  ptr += (byMode == 40) ? "rgb(200,45,0);'" : "#000;'";
  ptr += "><b>STROBO SLOW</b></button></a></td><td><a href=\'/41\'><button style='margin-top:auto;margin-bottom:auto;height:50px;width:400px;background-color:rgb(255,255,180);font-size:35px;font-family:Open Sans;color: ";
  ptr += (byMode == 41) ? "rgb(200,45,0);'" : "#000;'";
  ptr += "><b>STROBO MID</b></button></a></td><td><a href=\'/42\'><button style='margin-top:auto;margin-bottom:auto;height:50px;width:400px;background-color:rgb(255,255,180);font-size:35px;font-family:Open Sans;color: ";
  ptr += (byMode == 42) ? "rgb(200,45,0);'" : "#000;'";
  ptr += "><b>STROBO FAST</b></button></a></td></tr></table>";
  ptr += "<h2 style='margin-bottom:5px; margin-top: 0px; color: olive; font-size:22px; text-align: center; font-family: system-ui;'>Color strobo effects:</h2>";
  ptr += "<table style='text-align: center;margin-right: auto; margin-left:auto; width: 100%'>";
  ptr += "<tr><td><a href=\'/50\'><button style='margin-top:auto;margin-bottom:auto;height:50px;width:400px;background-color:rgb(255,255,180);font-size:35px;font-family:Open Sans;color: ";
  ptr += (byMode == 50) ? "rgb(200,45,0);'" : "#000;'";
  ptr += "><b>STROBO SLOW</b></button></a></td><td><a href=\'/51\'><button style='margin-top:auto;margin-bottom:auto;height:50px;width:400px;background-color:rgb(255,255,180);font-size:35px;font-family:Open Sans;color: ";
  ptr += (byMode == 51) ? "rgb(200,45,0);'" : "#000;'";
  ptr += "><b>STROBO MID</b></button></a></td><td><a href=\'/52\'><button style='margin-top:auto;margin-bottom:auto;height:50px;width:400px;background-color:rgb(255,255,180);font-size:35px;font-family:Open Sans;color: ";
  ptr += (byMode == 52) ? "rgb(200,45,0);'" : "#000;'";
  ptr += "><b>STROBO FAST</b></button></a></td></tr>";
  ptr += "</table>";
  ptr += "</body>";
  ptr += "</html>";
  return ptr;
}
//-------------- Server running config --------------
void handle_OnConnect() {
  byModeServer = byMode;
  DMX_Server.send(200, "text/html", SendHTML(byModeServer));
}
//-------------- Server 404 ERROR --------------
void handle_NotFound() {
  DMX_Server.send(404, "text/plain", "Oooops, that's bloomer!");
}
//-------------- Inicializace DMX512 protokolu --------------
void InitDmx()
{
  if(bySetupStep!=3)
  {
    Serial.println("This function shouldn't be called now! Terminating.");
    FirstScreen();
  }
  Serial.println("Initializing DMX universe.");
  DMX::Initialize(output);
  DMX::Write(iChannel,255);
  Serial.println("Done.");
  ShowHelp();
  bySetupStep = 4;
}

//-------------- Zobrazeni dostupnych prikazu --------------
void ShowHelp()
{
  Serial.println("\nAvailable commands are:");
  Serial.println("chan - sets number of first channel, default: 1");
  Serial.println("mode - selects color mode");
  //Serial.println("list - shows list of available modes");
  Serial.println("conn - returns WiFi status");
  Serial.println("wfcn - starts WiFi connection wizard to access Webserver");
  Serial.println("wfdc - disconnects and turns off WiFi and Webserver");
  Serial.println("debg - turns on/off debugging messages");
}

//-------------- Nastaveni Timer 1  --------------
void LoadDefaults()
{
  Timer1_Cfg = timerBegin(0, 8000, true); //10us
  timerAttachInterrupt(Timer1_Cfg, &Timer1_ISR, true);
  if(byMode==2)
  timerAlarmWrite(Timer1_Cfg, 1200, true);
  else
  timerAlarmWrite(Timer1_Cfg, 20000, true);
  timerAlarmEnable(Timer1_Cfg);
  Timer1_ISR();
}

//-------------- Dekodovani zpravy z prikazove radky (serial terminal) --------------
void LoopCommandProcessor(byte inByte)
{
  /*Serial.println("");
  Serial.println(inByte);
  Serial.println(input_pos);*/
  switch(inByte)
  {
    case '\n':
      input_line[input_pos] = '\0';
      int DataLenght;
      DataLenght = input_pos;
      input_pos = 0;
      bCommandRead = 0;
      Serial.flush();
      ProcessCommand(input_line, DataLenght);
    break;
    case '\r':
    break;
    case 0xFF:
    break;
    default:
      if(input_pos < (MAX_INPUT - 1))
      {
        bCommandRead = 1;
        lLastTime = millis();
        input_line[input_pos++] = inByte;
      }
      else
      {
        Serial.println("Too long!");
        input_pos = 0;
      }
    break;
  }
  if((Serial.available()==0)&&(input_pos>0)&&((lLastTime+600)<millis()))
  {
    input_line[input_pos] = '\0';
    int DataLenght;
    DataLenght = input_pos;
    input_pos = 0;
    bCommandRead = 0;
    Serial.flush();
    ProcessCommand(input_line, DataLenght);
  }
}

//-------------- Nastaveni modu osvetleni --------------
void ModeMenu()
{
  Serial.println("\nAvailable modes are:\n");
  Serial.println(" - 0 - Cycle\n");
  Serial.println(" - 1 - Pride\n");
  Serial.println(" - 2 - Rainbow\n");
  Serial.println(" - 3 - Color\n");
  Serial.println(" - 4 - Strobe\n");
  bMenu = 1;
}

void ChangeMode(int* TempMode)
{
  if(*TempMode==0)
  {
    timerStop(Timer1_Cfg);
    DMX::Write(iChannel+1,0);
    DMX::Write(iChannel+2,0);
    DMX::Write(iChannel+3,0);
    DMX::Write(iChannel+4,0);
    DMX::Write(iChannel+5,0);
    DMX::Write(iChannel+6,0);
    Serial.println("\nMode changed to: Cycle");
    timerAlarmWrite(Timer1_Cfg, 20000, true);
    timerRestart(Timer1_Cfg);
    timerStart(Timer1_Cfg);
    timerAlarmEnable(Timer1_Cfg);
    byMode = 0;
    bSender=0;
    Timer1_ISR();
  }
  else if(*TempMode==1)
  {
    timerStop(Timer1_Cfg);
    DMX::Write(iChannel+1,0);
    DMX::Write(iChannel+2,0);
    DMX::Write(iChannel+3,0);
    DMX::Write(iChannel+4,0);
    DMX::Write(iChannel+5,0);
    DMX::Write(iChannel+6,0);
    Serial.println("\nMode changed to: Pride");
    timerAlarmWrite(Timer1_Cfg, 10000, true);
    timerRestart(Timer1_Cfg);
    timerStart(Timer1_Cfg);
    timerAlarmEnable(Timer1_Cfg);
    byMode = 1;
    bSender=0;
    Timer1_ISR();
  }
  else if(*TempMode==2)
  {
    timerStop(Timer1_Cfg);
    bRedEnable=0;
    bGreenEnable=0;
    bBlueEnable=1;
    bRedDirection=1;
    bGreenDirection=0;
    bBlueDirection=1;
    byRed=255;
    byGreen=0;
    byBlue=250;
    DMX::Write(iChannel+1,byRed);
    DMX::Write(iChannel+2,byGreen);
    DMX::Write(iChannel+3,byBlue);
    DMX::Write(iChannel+4,0);
    DMX::Write(iChannel+5,0);
    DMX::Write(iChannel+6,0);
    Serial.println("\nMode changed to: Rainbow");
    timerAlarmWrite(Timer1_Cfg, 1200, true);
    timerRestart(Timer1_Cfg);
    timerStart(Timer1_Cfg);
    timerAlarmEnable(Timer1_Cfg);
    bSender=0;
    byMode=2;
  }
  else if(*TempMode==3)
  {
    timerStop(Timer1_Cfg);
    Serial.println("\nMode changed to: Color - static");
    if(bSender==0)
    {
      Serial.println("List of available colors:");
      Serial.println("1  - Red        2  - Green       3  - Blue      ");
      Serial.println("4  - White      5  - Orange      6  - Darkorange");
      Serial.println("7  - Salmon     8  - Cream       9  - Yellow    ");
      Serial.println("10 - Ivory      11 - Vest        12 - Lime      ");
      Serial.println("13 - Seagreen   14 - Teal        15 - Mint      ");
      Serial.println("16 - Caribbean  17 - Slate       18 - Pacific   ");
      Serial.println("19 - Violet     20 - Lightgreen  21 - Purple    ");
      Serial.println("22 - Barbie     23 - Broadway    24 - Firepink  ");
    }
    DMX::Write(iChannel+5,0);
    DMX::Write(iChannel+6,0);
    byMode = 3;
    bSubmenu = 1;
  }
  else if(*TempMode==4)
  {
    timerStop(Timer1_Cfg);
    Serial.println("\nMode changed to: Strobo");
    Serial.println("List of available combinations:");
    Serial.println("1X - Strobo when using actual color, X = <0;9>, 9 = fastest");
    Serial.println("2X - Strobo with buildin strobo white, X = <0;9>, 9 = fastest");
    byMode = 4;
    bSubmenu = 1;
  }
  else
  {
    Serial.printf("Sorry this mode doesn't exist.");
  }
  bMenu = 0;
}

//-------------- Prirazeni vyznamu k prikazu --------------
void ProcessCommand(char* Data, int DataLenght)
{
  sMessage = String(Data);
  if(sMessage=="?")
  {
    ShowHelp();
  }
  else if(sMessage=="wfcn") //WiFi connect
  {
    Serial.println("\nWiFi connection wizard loading!");
    WiFi.disconnect();
    WiFi_connect();
  }
  else if(sMessage=="wfdc") //WiFi disconnect
  {
    bServer=0;
    DMX_Server.stop();
    DMX_Server.close();
    WiFi.disconnect();  //For safety reasons, calling disconnect function
    WiFi.mode(WIFI_OFF);  //Turning WiFi off
    Serial.println("\nWiFi and Webserver were sucessfully turned off! Controller is now running in CLI mode only.");
  }
  else if(sMessage=="mode")
  {
    ModeMenu();
  }
  else if(sMessage=="chan")
  {
    int iNewChannel=0;
    for(;iNewChannel==0;)
    {
      while(Serial.available () > 0)
      {
        iNewChannel = Serial.parseInt();
      }
    }
    if((iNewChannel>0)&&(iNewChannel<512))
    iChannel = iNewChannel;
    Serial.print("\nDefault channel was changed to:");
    Serial.println(iChannel);
  }
  else if(sMessage=="conn")
  {
    if(WiFi.status()==255) //WL_NO_SHIELD
    {
      Serial.println("\nOnly CLI mode was selected at startup.");
    }
    else
    {
      switch(WiFi.status())
      {
        case WL_IDLE_STATUS:
          Serial.println("\nIdle, not connected to any network.");
        break;
        case WL_NO_SSID_AVAIL:
          Serial.println("\nNo SSID available!");
        break;
        case WL_SCAN_COMPLETED:
          Serial.println("\nSSID scan completed.");
        break;
        case WL_CONNECTED:
          Serial.print("\nController IP address: ");
          Serial.println(WiFi.localIP());
          Serial.print("\nController hostname: ");
          Serial.println(WiFi.getHostname());
        break;
        case WL_CONNECT_FAILED:
          Serial.println("\nConnection failed!");
        break;
        case WL_CONNECTION_LOST:
          Serial.println("\nConnection lost!");
        break;
        case WL_DISCONNECTED:
          Serial.println("\nDisconnected.");
        break;
      }
    }
  }
  else if(sMessage=="debg") //debugging messages
  {
    bDegubMess = !bDegubMess;
    if(bDegubMess==0)
      Serial.println("\nDebug messages turned off.");
    else
      Serial.println("\nDebug messages turned on.");
  }
  else
  {
    Serial.print(sMessage);
    Serial.println(" is unknown command, please check for any typos, or use '?' to display command list.");
  }
  for(int i = 0;i < DataLenght;i++)
  {
    Data[i]='\0';
  }
}

//-------------- Loop --------------
void loop()
{
  while((Serial.available () > 0)||(bCommandRead==1)||(bSender==1))
  {
    if(bMenu==1)  //cekam na mod  (hodnota)
    {
      while(Serial.available () > 0)
        {
          iTempMode = Serial.parseInt();
          ChangeMode(&iTempMode);
        }
    }
    else if(bSubmenu==1)  //cekam na submode (hodnota)
    {
      if(byMode==3) //Mode 3: Color
      {
        int iNewColor;
        if(bSender==0)
        {
          while(Serial.available () > 0)
          {
            iNewColor = Serial.parseInt();
          }
        }
        else
        {
          iNewColor = iServerTemp;
          //Serial.println(iServerTemp);
          //Serial.println(iNewColor);
          bSender=0;
        }
        if((iNewColor>=0)&&(iNewColor<=24))
        {
          byColor = iNewColor;
          //Serial.println("");
          //Serial.print(byColor);
          Serial.print("\nSuccess, new color is: ");
          Timer1_ISR();
          Serial.println(sColorName);
          bSubmenu = 0;
        }
        else
        {
          Serial.println("Error occured, enter color number again");
        }
      }
      else if(byMode==4)//Mode 4: Strobo
      {
        int iStrobo;
        if(bSender==0)
        {
          while(Serial.available () > 0)
          {
            iStrobo = Serial.parseInt();
          }
        }
        else
        {
          iStrobo = iServerTemp;
          bSender=0;
        }
        Serial.println(iStrobo);
        if((iStrobo>=10)&&(iStrobo<=29))
        {
          if((iStrobo/10)==1)
          {
            DMX::Write(iChannel+5,100);
            iStrobo -= 10;
            byStrobo = 10+iStrobo*27;
            DMX::Write(iChannel+6, byStrobo);
            Serial.print("\nSuccess, strobo set to:");
            Serial.print(byStrobo);
            bSubmenu = 0;
          }
          else if((iStrobo/10)==2)
          {
            DMX::Write(iChannel+5,200);
            iStrobo -= 20;
            byStrobo = 10+iStrobo*27;
            DMX::Write(iChannel+6, byStrobo);
            Serial.print("\nSuccess, strobo set to:");
            Serial.print(byStrobo);
            bSubmenu = 0;
          }
        }
        else
        {
          Serial.println("Error occured, enter color number again");
        }
      }
    }
    else
      LoopCommandProcessor(Serial.read ());
  }
  if((bISRHappened == 1)&&(bDegubMess==1))
  {
    if(byMode==0) Serial.println(sColorName);
    if(byMode==2)
    {
      Serial.print("R: ");
      Serial.print(byRed);
      Serial.print(" G: ");
      Serial.print(byGreen);
      Serial.print(" B: ");
      Serial.println(byBlue);
    }
    bISRHappened = 0;
  }
  if(bServer==1)
  {
    DMX_Server.handleClient();
  }
}

//-------------- Timer 1 (ISR - DMX512 Color Control) --------------
void IRAM_ATTR Timer1_ISR()
{
  if((byMode==0)||(byMode==3))
  {
    switch(byColor)
    {
      case 1:
      //red
      sColorName = "Red";
      DMX::Write(iChannel+1,255);
      DMX::Write(iChannel+2,0);
      DMX::Write(iChannel+3,0);
      DMX::Write(iChannel+4,0);
      break;
      case 2:
      //green
      sColorName = "Green";
      DMX::Write(iChannel+1,0);
      DMX::Write(iChannel+2,255);
      DMX::Write(iChannel+3,0);
      DMX::Write(iChannel+4,0);
      break;
      case 3:
      //blue
      sColorName = "Blue";
      DMX::Write(iChannel+1,0);
      DMX::Write(iChannel+2,0);
      DMX::Write(iChannel+3,255);
      DMX::Write(iChannel+4,0);
      break;
      case 4:
      //white
      sColorName = "White";
      DMX::Write(iChannel+1,0);
      DMX::Write(iChannel+2,0);
      DMX::Write(iChannel+3,0);
      DMX::Write(iChannel+4,255);
      break;
      case 5:
      //darkorange
      sColorName = "Orange";
      DMX::Write(iChannel+1,255);
      DMX::Write(iChannel+2,110);
      DMX::Write(iChannel+3,0);
      DMX::Write(iChannel+4,0);
      break;
      case 6:
      //nectarine
      sColorName = "Darkorange";
      DMX::Write(iChannel+1,255);
      DMX::Write(iChannel+2,51);
      DMX::Write(iChannel+3,0);
      DMX::Write(iChannel+4,0);
      break;
      case 7:
      //salmon
      sColorName = "Salmon";
      DMX::Write(iChannel+1,255);
      DMX::Write(iChannel+2,135);
      DMX::Write(iChannel+3,10);
      DMX::Write(iChannel+4,15);
      break;
      case 8:
      //cream
      sColorName = "Cream";
      DMX::Write(iChannel+1,255);
      DMX::Write(iChannel+2,160);
      DMX::Write(iChannel+3,0);
      DMX::Write(iChannel+4,45);
      break;
      case 9:
      //orange
      sColorName = "Yellow";
      DMX::Write(iChannel+1,220);
      DMX::Write(iChannel+2,140);
      DMX::Write(iChannel+3,5);
      DMX::Write(iChannel+4,10);
      break;
      case 10:
      //gold -TBD
      sColorName = "Ivory";
      DMX::Write(iChannel+1,225);
      DMX::Write(iChannel+2,173);
      DMX::Write(iChannel+3,23);
      DMX::Write(iChannel+4,0);
      break;
      case 11:
      //vest
      sColorName = "Vest";
      DMX::Write(iChannel+1,179);
      DMX::Write(iChannel+2,245);
      DMX::Write(iChannel+3,18);
      DMX::Write(iChannel+4,0);
      break;
      case 12:
      //lawn
      sColorName = "Lime";
      DMX::Write(iChannel+1,125);
      DMX::Write(iChannel+2,252);
      DMX::Write(iChannel+3,0);
      DMX::Write(iChannel+4,0);
      break;
      case 13:
      //lime
      sColorName = "Seagreen";
      DMX::Write(iChannel+1,51);
      DMX::Write(iChannel+2,255);
      DMX::Write(iChannel+3,51);
      DMX::Write(iChannel+4,0);
      break;
      case 14:
      //spring
      sColorName = "Teal";
      DMX::Write(iChannel+1,0);
      DMX::Write(iChannel+2,255);
      DMX::Write(iChannel+3,51);
      DMX::Write(iChannel+4,0);
      break;
      case 15:
      //mint
      sColorName = "Mint";
      DMX::Write(iChannel+1,0);
      DMX::Write(iChannel+2,240);
      DMX::Write(iChannel+3,180);
      DMX::Write(iChannel+4,70);
      break;
      case 16:
      //caribbean
      sColorName = "Caribbean";
      DMX::Write(iChannel+1,0);
      DMX::Write(iChannel+2,255);
      DMX::Write(iChannel+3,204);
      DMX::Write(iChannel+4,0);
      break;
      case 17:
      //slate
      sColorName = "Slate";
      DMX::Write(iChannel+1,0);
      DMX::Write(iChannel+2,128);
      DMX::Write(iChannel+3,255);
      DMX::Write(iChannel+4,0);
      break;
      case 18:
      //pacific
      sColorName = "Pacific";
      DMX::Write(iChannel+1,0);
      DMX::Write(iChannel+2,55);
      DMX::Write(iChannel+3,255);
      DMX::Write(iChannel+4,0);
      break;
      case 19:
      //violet
      sColorName = "Violet";
      DMX::Write(iChannel+1,51);
      DMX::Write(iChannel+2,0);
      DMX::Write(iChannel+3,255);
      DMX::Write(iChannel+4,0);
      break;
      case 20:
      //lightgreen
      sColorName = "Lightgreen";
      DMX::Write(iChannel+1,0);
      DMX::Write(iChannel+2,204);
      DMX::Write(iChannel+3,0);
      DMX::Write(iChannel+4,250);
      break;
      case 21:
      //purple
      sColorName = "Purple";
      DMX::Write(iChannel+1,128);
      DMX::Write(iChannel+2,0);
      DMX::Write(iChannel+3,255);
      DMX::Write(iChannel+4,0);
      break;
      case 22:
      //barbie
      sColorName = "Barbie";
      DMX::Write(iChannel+1,255);
      DMX::Write(iChannel+2,0);
      DMX::Write(iChannel+3,255);
      DMX::Write(iChannel+4,0);
      break;
      case 23:
      //broadway
      sColorName = "Broadway";
      DMX::Write(iChannel+1,255);
      DMX::Write(iChannel+2,0);
      DMX::Write(iChannel+3,102);
      DMX::Write(iChannel+4,0);
      break;
      case 24:
      //firepink
      sColorName = "Firepink";
      DMX::Write(iChannel+1,255);
      DMX::Write(iChannel+2,0);
      DMX::Write(iChannel+3,51);
      DMX::Write(iChannel+4,0);
      break;
    }
    if(byMode==0)
    {
      byColor++;
      if(byColor==25)byColor=1;
    }
  }
  if(byMode==1)
  {
    switch(byPride)
    {
      case 0:
      DMX::Write(iChannel,230);
      DMX::Write(iChannel+1,255);
      DMX::Write(iChannel+3,0);
      break;
      case 1:
      DMX::Write(iChannel,255);
      DMX::Write(iChannel+1,255);
      DMX::Write(iChannel+2,51);
      DMX::Write(iChannel+3,0);
      break;
      case 2:
      DMX::Write(iChannel+1,220);
      DMX::Write(iChannel+2,140);
      DMX::Write(iChannel+4,5);
      break;
      case 3:
      DMX::Write(iChannel+4,0);
      DMX::Write(iChannel+1,40);
      DMX::Write(iChannel+2,255);
      DMX::Write(iChannel+3,0);
      break;
      case 4:
      DMX::Write(iChannel+1,0);
      DMX::Write(iChannel+2,20);
      DMX::Write(iChannel+3,255);
      break;
      case 5:
      DMX::Write(iChannel+2,0);
      DMX::Write(iChannel+1,70);
      break;
    }
    byPride++;
    if(byPride>=6) byPride=0;
  }
  if(byMode==2)
  {
    if(bRedEnable==1)
    {
      if(bRedDirection==1)
      {
        if(byRed==255)
        {
          bRedDirection=0;
          bRedEnable=0;
          bBlueEnable=1;	//aktivuje blue fadeout
        }
        else
        {
          if(byRed<8)
          byRed++;
          else if(byRed<20)
          byRed+=2;
          else if((byRed>=100)&&(byRed<200))
          byRed+=10;
          else
          byRed+=5;
        }
      }
      else
      {
        if(byRed==0)
        {
          bRedDirection=1;
          bRedEnable=0;
          bBlueEnable=1;	//aktivuje blue fadein
        }
        else
        {
          if(byRed<=10)
          byRed--;
          else if(byRed<=20)
          byRed-=2;
          else if((byRed>100)&&(byRed<=200))
          byRed-=10;
          else
          byRed-=5;
        }
      }
    }

    if(bGreenEnable==1)
    {
      if(bGreenDirection==1)
      {
        if(byGreen==255)
        {
          bGreenDirection=0;
          bGreenEnable=0;
          bRedEnable=1;	//aktivuje red fadeout
        }
        else
        {
          if(byGreen<10)
          byGreen++;
          else if(byGreen<20)
          byGreen+=2;
          else if((byGreen>=100)&&(byGreen<200))
          byGreen+=10;
          else
          byGreen+=5;
        }
      }
      else
      {
        if(byGreen==0)
        {
          bGreenDirection=1;
          bGreenEnable=0;
          bRedEnable=1;	//aktivuje red fadein
        }
        else
        {
          if(byGreen<=2)
          byGreen--;
          else if(byGreen<=10)
          byGreen-=2;
          else if(byGreen<=30)
          byGreen-=4;
          else if((byGreen>100)&&(byGreen<245))
          byGreen-=10;
          else
          byGreen-=7;
        }
      }
    }

    if(bBlueEnable==1)
    {
      if(bBlueDirection==1)
      {
        if((byBlue==255))
        {
          bBlueDirection=0;
          bBlueEnable=0;
          bGreenEnable=1;	//aktivuje green fadeout
        }
        else
        {
          if(byBlue<10)
          byBlue++;
          else if(byBlue<20)
          byBlue+=2;
          else if((byBlue>=100)&&(byBlue<200))
          byBlue+=10;
          else
          byBlue+=5;
        }
      }
      else
      {
        if(byBlue==0)
        {
          bBlueDirection=1;
          bBlueEnable=0;
          bGreenEnable=1;	//aktivuje green fadein
        }
        else
        {
          if(byBlue<=10)
          byBlue--;
          else if(byBlue<=20)
          byBlue-=2;
          else if((byBlue>100)&&(byBlue<=200))
          byBlue-=10;
          else
          byBlue-=5;
        }
      }
    }
    DMX::Write(iChannel+1,byRed);
    DMX::Write(iChannel+2,byGreen);
    DMX::Write(iChannel+3,byBlue);
  }
  bISRHappened = 1;
}