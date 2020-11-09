#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <ETH.h>
#include <SPIFFS.h>
#include <Update.h>
#include <esp_wifi.h>

HardwareSerial USE_SERIAL1(1);

#define RX1 36
#define TX1 4
#define TRIGGER_PIN 16
//create UDP instance
WiFiUDP udp;
//create TCP Server
WiFiServer TCPserver(10001);
//create Async Web Server
AsyncWebServer server(80);

const char* passwordAP = "metrici@admin";
IPAddress local_IP_AP(109,108,112,114); //decimal for "mlpr"(metrici license plate recognition) in ASCII table
IPAddress gatewayAP(0,0,0,0);
IPAddress subnetAP(255,255,255,0);

//UDP Credentials
String Server_IP = "0.0.0.0"; 
String Server_Port = "0000"; //server port

static bool eth_connected = false;
const char* host_name = "MetriciRadarController";

bool isAPmodeOn = false;
bool shouldReboot = false;
String value_login[3];
bool userFlag = false;
String Controller_IP = "";
String Controller_Port = "10001";
String Units = "KPH";
String TriggerSpeed = "30";
String Direction = "Towards";
String Threshold = "0.15";

String strlog;

//------------------------- struct circular_buffer
struct ring_buffer
{
    ring_buffer(size_t cap) : buffer(cap) {}

    bool empty() const { return sz == 0 ; }
    bool full() const { return sz == buffer.size() ; }

    void push( String str )
    {
        if(last >= buffer.size()) last = 0 ;
        buffer[last] = str ;
        ++last ;
        if(full()) 
			first = (first+1) %  buffer.size() ;
        else ++sz ;
    }
    void print() const {
		strlog= "";
		if( first < last )
			for( size_t i = first ; i < last ; ++i ) {
				strlog += (buffer[i] + "<br>");
			}	
		else {
			for( size_t i = first ; i < buffer.size() ; ++i ) {
				strlog += (buffer[i] + "<br>");
			}
			for( size_t i = 0 ; i < last ; ++i ) {
				strlog += (buffer[i] + "<br>");
			}
		}
	}

    private:
        std::vector<String> buffer ;
        size_t first = 0 ;
        size_t last = 0 ;
        size_t sz = 0 ;
};
//------------------------- struct circular_buffer
ring_buffer circle(10);

//------------------------- logOutput(String)
void logOutput(String string1) {
	circle.push(string1);
	Serial.println(string1);
}
//------------------------- processor()
String processor(const String& var) {
	circle.print();
	if  (var == "PLACEHOLDER_LOGS")
		return strlog;
  else if (var == "PH_Server_IP")
    return Server_IP;
  else if (var == "PH_Server_Port")
    return Server_Port;
  else if (var == "PH_Controller_IP")
    return Controller_IP;
  else if (var == "PH_Controller_Port")
    return Controller_Port;
  else if (var == "PH_Speed_Unit")
    return Units;
  else if (var == "PH_Min_Speed")
    return TriggerSpeed;
  else if (var == "PH_Direction")
    return Direction;
  else if (var == "PH_Threshold")
    return Threshold;
  else if (var == "PH_Version")
    return String("v1.1");
	return String();  
}

//------------------------- listAllFiles()
void listAllFiles() {
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while(file){
    Serial.print("FILE: ");
    String fileName = file.name();
    Serial.println(fileName);
    file = root.openNextFile();
  }
  file.close();
  root.close();
}

//------------------------- fileReadLines()
void fileReadLines(File file, String x[]) {
  int i = 0;
    while(file.available()){
      String line= file.readStringUntil('\n');
      line.trim();
      x[i] = line;
      i++;
      // logOutput((String)"Line: " + line);
    }
}

String readString(File s) {
  // Read from file witout yield 
  String ret;
  int c = s.read();
  while (c >= 0) {
    ret += (char)c;
    c = s.read();
  }
  return ret;
}

// Add files to the /files table
String& addDirList(String& HTML) {
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  int count = 0;
  while (file) {
    String fileName = file.name();
    File dirFile = SPIFFS.open(fileName, "r");
    size_t filelen = dirFile.size();
    dirFile.close();
    String filename_temp = fileName;
    filename_temp.replace("/", "");
    String directories = "<form action=\"/files\" method=\"POST\">\r\n<tr><td align=\"center\">";
    if(fileName.indexOf("Radar") > 0) { // Check if the selected file's name contains 'Radar' 
      directories += "<input type=\"hidden\" name = \"filename\" value=\"" + fileName + "\">" + filename_temp;
      directories += "</td>\r\n<td align=\"center\">" + String(filelen, DEC);
      directories += "</td><td align=\"center\"><button style=\"margin-right:20px;\" type=\"submit\" name= \"download\">Download</button><button type=\"submit\" name= \"delete\">Delete</button></td>\r\n";
      directories += "</tr></form>\r\n~directories~";
      HTML.replace("~directories~", directories);
      count++;
    }
    file = root.openNextFile();
  }
  HTML.replace("~directories~", "");
  
  HTML.replace("~count~", String(count, DEC));
  HTML.replace("~total~", String(SPIFFS.totalBytes() / 1024, DEC));
  HTML.replace("~used~", String(SPIFFS.usedBytes() / 1024, DEC));
  HTML.replace("~free~", String((SPIFFS.totalBytes() - SPIFFS.usedBytes()) / 1024, DEC));

  return HTML;
}

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
  if(filename.indexOf(".bin") > 0) {
    if (!index){
      logOutput("The update process has started...");
      // if filename includes spiffs, update the spiffs partition
      int cmd = (filename.indexOf("spiffs") > -1) ? U_SPIFFS : U_FLASH;
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
        Update.printError(Serial);
      }
    }

    if (Update.write(data, len) != len) {
      Update.printError(Serial);
    }

    if (final) {
      if (filename.indexOf("spiffs") > -1) {
        request->send(200, "text/html", "<div style=\"margin:0 auto; text-align:center; font-family:arial;\">The device entered AP Mode ! Please connect to it.</div>");  
      } else {
        request->send(200, "text/html", "<div style=\"margin:0 auto; text-align:center; font-family:arial;\">Congratulation ! </br> You have successfully updated the device to the latest version. </br>Please wait 10 seconds until the device reboots, then press on the \"Go Home\" button to go back to the main page.</br></br> <form method=\"post\" action=\"http://" + Controller_IP + "\"><input type=\"submit\" name=\"goHome\" value=\"Go Home\"/></form></div>");
      }

      if (!Update.end(true)){
        Update.printError(Serial);
      } else {
        logOutput("Update complete");
        Serial.flush();
        USE_SERIAL1.flush();
        ESP.restart();
      }
    }
  } else {
      if(!index){
        logOutput((String)"Started uploading: " + filename);
        // open the file on first call and store the file handle in the request object
        request->_tempFile = SPIFFS.open("/"+filename, "w");
      }
      if(len) {
        // stream the incoming chunk to the opened file
        request->_tempFile.write(data,len);
      }
      if(final){
        logOutput((String)filename + " was successfully uploaded! File size: " + index+len);
        // close the file handle as the upload is now done
        request->_tempFile.close();
        request->redirect("/files");
      }
    }
}

//------------------------- void EthernetConfig(String x[])
void EthernetConfig(String x[]){
  if(x[0] == "DHCP IP") {
    logOutput((String)"Obtaining DHCP IP !");
  } else if(x[0] != NULL &&      //Local IP
     x[0].length() != 0 &&
     x[1] != NULL &&             // Gateway
     x[1].length() != 0 &&
     x[2] != NULL &&             // Subnet
     x[2].length() != 0 &&
     x[3] != NULL &&             // DNS
     x[3].length() != 0) {
       IPAddress local_IP_STA, gateway_STA, subnet_STA, primaryDNS;
       local_IP_STA.fromString(x[0]);
       gateway_STA.fromString(x[1]);
       subnet_STA.fromString(x[2]);
       primaryDNS.fromString(x[3]);
       if(!ETH.config(local_IP_STA, gateway_STA, subnet_STA, primaryDNS)) {
         logOutput((String)"Couldn't configure STATIC IP ! Obtaining DHCP IP !");
       }
       delay(50);
  }
}

void WiFiEvent(WiFiEvent_t event){
  switch (event) {
    case SYSTEM_EVENT_ETH_START:
      Serial.println("ETH Started");
      //set eth hostname here
      ETH.setHostname(host_name);
      break;
    case SYSTEM_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;
    case SYSTEM_EVENT_ETH_GOT_IP:
      eth_connected = true;
      Serial.print("ETH MAC: ");
      Serial.print(ETH.macAddress());
      Serial.print(", IPv4: ");
      Serial.print(ETH.localIP());
      if (ETH.fullDuplex()) {
        Serial.print(", FULL_DUPLEX");
      }
      Serial.print(", ");
      Serial.print(ETH.linkSpeed());
      Serial.println("Mbps");
      break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;
    case SYSTEM_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_connected = false;
      break;
    default:
      break;
  }
}

//------------------------- startAP()
void startAP() {
  delay(50);
  if(SPIFFS.exists("/networkRadar.txt")) SPIFFS.remove("/networkRadar.txt");  

  String x = "Metrici";
  x.concat(random(100,999));
  char ssidAP[x.length()+1];
  x.toCharArray(ssidAP,sizeof(ssidAP));

  logOutput((String)"Starting AP ... ");
  logOutput(WiFi.softAP(ssidAP, passwordAP) ? (String) ssidAP + " ready" : "WiFi.softAP failed ! (Password must be at least 8 characters long )");
  delay(500);
  logOutput((String)"Setting AP configuration ... ");
  logOutput(WiFi.softAPConfig(local_IP_AP, gatewayAP, subnetAP) ? "Ready" : "Failed!");
  delay(500);
  logOutput((String)"Soft-AP IP address: ");
  logOutput(WiFi.softAPIP().toString());
  // digitalWrite(LED, HIGH);
  delay(500);
  
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/favicon.ico", "image/ico");
  });
  server.on("/newMaster.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/newMaster.css", "text/css");
  });
  server.on("/logo.png", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/logo.png", "image/png");
  });
  server.on("/events_placeholder.html", HTTP_GET, [](AsyncWebServerRequest* request){
    request->send(SPIFFS, "/events_placeholder.html", "text/html", false, processor);
  });

  server.on("/register", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/createUser.html", "text/html", false, processor);
  });
  server.on("/chooseIP", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/chooseIP.html", "text/html", false, processor);
  });
  server.on("/dhcpIP", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/dhcpIP_AP.html", "text/html", false, processor);
  });
  server.on("/staticIP", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/staticIP_AP.html", "text/html", false, processor);
  });
  server.on("/logs", HTTP_GET, [](AsyncWebServerRequest* request){
    request->send(SPIFFS, "/events.html", "text/html", false, processor);
  });

  server.on("/register", HTTP_POST, [](AsyncWebServerRequest * request){
    if(request->hasArg("register")){
      int params = request->params();
      String values_user[params];
      for(int i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()){
            logOutput((String)"POST[" + p->name().c_str() + "]: " + p->value().c_str() + "\n");            
            values_user[i] = p->value();            
          } else {
              logOutput((String)"GET[" + p->name().c_str() + "]: " + p->value().c_str() + "\n");
            }
      } // for(int i=0;i<params;i++)
      
      if(values_user[0] != NULL && values_user[0].length() > 4 &&
        values_user[1] != NULL && values_user[1].length() > 7) {
            File userWrite = SPIFFS.open("/userRadar.txt", "w");
            if(!userWrite) logOutput((String)"ERROR_INSIDE_POST ! Couldn't open file to write USER credentials !");
            userWrite.println(values_user[0]);  // Username
            userWrite.println(values_user[1]);  // Password
            userWrite.close();
            logOutput("Username and password saved !");          
            request->redirect("/chooseIP");
            // digitalWrite(LED, LOW);
      } else request->redirect("/register");  
    } else if (request->hasArg("skip")) {
      request->redirect("/chooseIP");
    } else if(request->hasArg("import")) {
      request->redirect("/files");
    } else {
      request->redirect("/register");
    }
  }); // server.on("/register", HTTP_POST, [](AsyncWebServerRequest * request)

  server.on("/files", HTTP_ANY, [](AsyncWebServerRequest *request){
    // Serial.print("/files, request: ");
    // Serial.println(request->methodToString());

    if (request->hasParam("filename", true)) { // Check for files
      if (request->hasArg("download")) { // Download file
        Serial.println("Download Filename: " + request->arg("filename"));
        AsyncWebServerResponse *response = request->beginResponse(SPIFFS, request->arg("filename"), String(), true);
        response->addHeader("Server", "ESP Async Web Server");
        request->send(response);
        return;
      } else if(request->hasArg("delete")) { // Delete file
        if (SPIFFS.remove(request->getParam("filename", true)->value())) {
          logOutput((String)request->getParam("filename", true)->value().c_str() + " was deleted !");
        } else {
          logOutput("Could not delete file. Try again !");
        }
        request->redirect("/files");
      }
    } else if(request->hasArg("restart_device")) {
        request->send(200,"text/plain", "The device will reboot shortly !");
        // ESP.restart();
        shouldReboot = true;
    }

    String HTML PROGMEM; // HTML code 
    File pageFile = SPIFFS.open("/files_AP.html", "r");
    if (pageFile) {
      HTML = readString(pageFile);
      pageFile.close();
      HTML = addDirList(HTML);
      AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", HTML.c_str(), processor);
      response->addHeader("Server","ESP Async Web Server");
      request->send(response);
    }
  });

  server.on("/staticIP", HTTP_POST, [](AsyncWebServerRequest * request){
    if(request->hasArg("saveStatic")){
      int params = request->params();      
      String values_static[8];
      for(int i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()){
            logOutput((String)"POST[" + p->name().c_str() + "]: " + p->value().c_str() + "\n");
            values_static[i] = p->value();            
          } else {
              logOutput((String)"GET[" + p->name().c_str() + "]: " + p->value().c_str() + "\n");
            }
      } // for(int i=0;i<params;i++)

      if(values_static[0] != NULL &&
        values_static[0].length() != 0 &&
        values_static[1] != NULL &&
        values_static[1].length() != 0 &&
        values_static[2] != NULL &&
        values_static[2].length() != 0 &&
        values_static[3] != NULL &&
        values_static[3].length() != 0) {
            File inputsWrite = SPIFFS.open("/networkRadar.txt", "w");
            if(!inputsWrite) logOutput((String)"ERROR: Couldn't open file to write Static IP credentials !");
            inputsWrite.println(values_static[0]);   // Local IP
            inputsWrite.println(values_static[1]);   // Gateway
            inputsWrite.println(values_static[2]);   // Subnet
            inputsWrite.println(values_static[3]);   // DNS
            inputsWrite.close();
            logOutput("Configuration saved !");
            request->redirect("/logs");
            shouldReboot = true;
      } else request->redirect("/staticIP");
    } else {
      request->redirect("/staticIP");
    }
  }); // server.on("/staticLogin", HTTP_POST, [](AsyncWebServerRequest * request)

  server.on("/dhcpIP", HTTP_POST, [](AsyncWebServerRequest * request){
    if(request->hasArg("saveDHCP")){      
      File inputsWrite = SPIFFS.open("/networkRadar.txt", "w");
      if(!inputsWrite) logOutput((String)"ERROR_INSIDE_POST ! Couldn't open file to write DHCP IP credentials !");
      inputsWrite.println("DHCP IP");  // Type of IP ? DHCP : Static
      inputsWrite.close();
      logOutput("Configuration saved !");
      request->redirect("/logs");      
      shouldReboot = true;
    } else {
      request->redirect("/dhcpIP");        
    }
  }); // server.on("/dhcpLogin", HTTP_POST, [](AsyncWebServerRequest * request)
  
  server.onFileUpload(handleUpload);
  server.onNotFound([](AsyncWebServerRequest *request){ request->redirect("/register"); });

  server.begin(); //-------------------------------------------------------------- server.begin()

} // void startAP()

void setup() {
  enableCore1WDT();
  delay(100);
  Serial.begin(115200);
  delay(1000);
  USE_SERIAL1.begin(9600, SERIAL_8N1, RX1, TX1);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  digitalWrite(TRIGGER_PIN, HIGH);
  Serial.println(digitalRead(TRIGGER_PIN));

  WiFi.onEvent(WiFiEvent);

  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS ! Formatting in progress");
    return;
  }

  listAllFiles();

  //--- Check if /networkRadar.txt exists. If not then create one
  if(!SPIFFS.exists("/networkRadar.txt")) {
    File inputsCreate = SPIFFS.open("/networkRadar.txt", "w");
    if(!inputsCreate) logOutput((String)"Couldn't create /networkRadar.txt");
    logOutput("Was /networkRadar.txt created ?");
    logOutput(SPIFFS.exists("/networkRadar.txt") ? "Yes" : "No");
    inputsCreate.close();
  }
  
  //--- Check if /networkRadar.txt is empty (0 bytes) or not (>8)
  //--- println adds /r and /n, which means 2 bytes
  //--- 2 lines = 2 println = 4 bytes
  File networkRead = SPIFFS.open("/networkRadar.txt");
  if(!networkRead) logOutput((String)"ERROR ! Couldn't open file to read !");

  if(networkRead.size() > 5) {
    //--- Read SSID, Password, Local IP, Gateway, Subnet, DNS from file
    //--- and store them in v[]
    String v[7];
    fileReadLines(networkRead,v);
    networkRead.close();
    ETH.begin();
    int ki = 0;
    while(!eth_connected && ki <20) {
      Serial.println("Establishing ETHERNET Connection ... ");
      delay(1000);
      ki++;
    }
    if(!eth_connected) {
      logOutput((String)"(1) Could not access Network ! Trying again...");
      logOutput((String)"Controller will restart in 1 second !");
      delay(1000);
      ESP.restart();
    }
    EthernetConfig(v);
    Controller_IP = ETH.localIP().toString();
    logOutput((String)"IP addres: " + Controller_IP);
    logOutput((String)"Gateway: " + ETH.gatewayIP().toString());
    logOutput((String)"Subnet: " + ETH.subnetMask().toString());
    logOutput((String)"DNS: " + ETH.dnsIP().toString());

    if(SPIFFS.exists("/userRadar.txt")) {
      File userRead = SPIFFS.open("/userRadar.txt", "r");
      if(!userRead) logOutput((String)"Couldn't read /userRadar.txt");
      fileReadLines(userRead, value_login);
      if(value_login[0].length() >4 && value_login[0] != NULL &&
          value_login[1].length() >7 && value_login[1] != NULL) 
          {
            userFlag = true;
          }
    }
  
    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(SPIFFS, "/favicon.ico", "image/ico");
    });
    server.on("/events_placeholder.html", HTTP_GET, [](AsyncWebServerRequest* request){
      request->send(SPIFFS, "/events_placeholder.html", "text/html", false, processor);
    });
    server.on("/newMaster.css", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(SPIFFS, "/newMaster.css", "text/css");
    });
    server.on("/jquery-1.12.4.min.js", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(SPIFFS, "/jquery-1.12.4.min.js", "text/javascript");
    });
    server.on("/logo.png", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(SPIFFS, "/logo.png", "image/png");
    });
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->redirect("/home");
    });
    server.on("/home", HTTP_GET, [](AsyncWebServerRequest* request){
      if(userFlag) {
        if(!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
          return request->requestAuthentication(NULL,false);
        request->send(SPIFFS, "/index.html", "text/html", false, processor);
      } else {          
        request->send(SPIFFS, "/index.html", "text/html", false, processor);
      }
    });
    server.on("/radar", HTTP_GET, [](AsyncWebServerRequest* request){
      if(userFlag) {
        if(!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
          return request->requestAuthentication(NULL,false);
        request->send(SPIFFS, "/radar_settings.html", "text/html", false, processor);
      } else {          
        request->send(SPIFFS, "/radar_settings.html", "text/html", false, processor);
      }
    });

    server.on("/dhcpIP", HTTP_GET, [](AsyncWebServerRequest *request){
      if(userFlag) {
        if(!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
          return request->requestAuthentication(NULL,false);            
        request->send(SPIFFS, "/dhcpIP_STA.html", "text/html", false, processor);
      } else {
        request->send(SPIFFS, "/dhcpIP_STA.html", "text/html", false, processor);
      }
    });
    server.on("/staticIP", HTTP_GET, [](AsyncWebServerRequest *request){
      if(userFlag) {
        if(!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
          return request->requestAuthentication(NULL,false);            
        request->send(SPIFFS, "/staticIP_STA.html", "text/html", false, processor);
      } else {
        request->send(SPIFFS, "/staticIP_STA.html", "text/html", false, processor);
      }
    });

    server.on("/home", HTTP_POST, [](AsyncWebServerRequest * request){
      if(request->hasArg("save_values")){
        request->redirect("/home");
        int params = request->params();
        for(int i=0;i<params;i++){
          AsyncWebParameter* p = request->getParam(i);
            if(p->isPost() && p->value() != NULL && p->name() != "save_values") {
              File configWrite = SPIFFS.open("/configRadar.txt", "w");
              if(!configWrite) logOutput((String)"ERROR: Couldn't open configRadar to save values from POST !");
                logOutput((String)"POST[" + p->name().c_str() + "]: " + p->value().c_str());
              if(p->name() == "getServerIP") {
                Server_IP = p->value();
                configWrite.println(Server_IP);
                configWrite.println(Server_Port);
                configWrite.println(Units);
                configWrite.println(TriggerSpeed);
                configWrite.println(Direction);
                configWrite.println(Threshold);
              }
              if(p->name() == "getServerPort") {
                Server_Port = p->value();
                configWrite.println(Server_IP);
                configWrite.println(Server_Port);
                configWrite.println(Units);
                configWrite.println(TriggerSpeed);
                configWrite.println(Direction);
                configWrite.println(Threshold);
              }
                configWrite.close();
              }
        } // for(int i=0;i<params;i++)
      } else {
        request->redirect("/home");
      }
    });

    server.on("/files", HTTP_ANY, [](AsyncWebServerRequest *request) {
      if(userFlag) {
        if(!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
          return request->requestAuthentication(NULL,false);            
        if (request->hasParam("filename", true)) { // Download file
          if (request->hasArg("download")) { // File download
            Serial.println("Download Filename: " + request->arg("filename"));
            AsyncWebServerResponse *response = request->beginResponse(SPIFFS, request->arg("filename"), String(), true);
            response->addHeader("Server", "ESP Async Web Server");
            request->send(response);
            return;
          } else if(request->hasArg("delete")) { // Delete file
            if (SPIFFS.remove(request->getParam("filename", true)->value())) {
              logOutput((String)request->getParam("filename", true)->value().c_str() + " was deleted !");
            } else {
              logOutput("Could not delete file. Try again !");
            }
            request->redirect("/files");
          }
        } else if(request->hasArg("restart_device")) {
          request->send(200,"text/plain", "The device will reboot shortly !");
          // ESP.restart();
          shouldReboot = true;
        }
        String HTML PROGMEM; // HTML code 
        File pageFile = SPIFFS.open("/files_STA.html", "r");
        if (pageFile) {
          HTML = readString(pageFile);
          pageFile.close();
          HTML = addDirList(HTML);
          AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", HTML.c_str(), processor);
          response->addHeader("Server","ESP Async Web Server");
          request->send(response);
        }
      } else {
        if (request->hasParam("filename", true)) { // Download file
          if (request->hasArg("download")) { // File download
            Serial.println("Download Filename: " + request->arg("filename"));
            AsyncWebServerResponse *response = request->beginResponse(SPIFFS, request->arg("filename"), String(), true);
            response->addHeader("Server", "ESP Async Web Server");
            request->send(response);
            return;
          } else if(request->hasArg("delete")) { // Delete file
            if (SPIFFS.remove(request->getParam("filename", true)->value())) {
              logOutput((String)request->getParam("filename", true)->value().c_str() + " was deleted !");
            } else {
              logOutput("Could not delete file. Try again !");
            }
            request->redirect("/files");
          }
        } else if(request->hasArg("restart_device")) {
          request->send(200,"text/plain", "The device will reboot shortly !");
          // ESP.restart();
          shouldReboot = true;
        }
        String HTML PROGMEM; // HTML code
        File pageFile = SPIFFS.open("/files_STA.html", "r");
        if (pageFile) {
          HTML = readString(pageFile);
          pageFile.close();
          HTML = addDirList(HTML);
          AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", HTML.c_str(), processor);
          response->addHeader("Server","ESP Async Web Server");
          request->send(response);         
        }
      }
    }); // server.on("/files", HTTP_ANY, [](AsyncWebServerRequest *request)

    server.on("/staticIP", HTTP_POST, [](AsyncWebServerRequest * request){
      if(request->hasArg("saveStatic")){
        int params = request->params();
        String values_static[8];
        for(int i=0;i<params;i++){
          AsyncWebParameter* p = request->getParam(i);
          if(p->isPost()){
              logOutput((String)"POST[" + p->name().c_str() + "]: " + p->value().c_str() + "\n");
              values_static[i] = p->value();            
            } else {
                logOutput((String)"GET[" + p->name().c_str() + "]: " + p->value().c_str() + "\n");
              }
        } // for(int i=0;i<params;i++)

        if(values_static[0] != NULL &&
          values_static[0].length() != 0 &&
          values_static[1] != NULL &&
          values_static[1].length() != 0 &&
          values_static[2] != NULL &&
          values_static[2].length() != 0 &&
          values_static[3] != NULL &&
          values_static[3].length() != 0) {
              File inputsWrite = SPIFFS.open("/networkRadar.txt", "w");
              if(!inputsWrite) logOutput((String)"ERROR: Couldn't open networkRadar to write Static IP credentials !");
              inputsWrite.println(values_static[0]);   // Local IP
              inputsWrite.println(values_static[1]);   // Gateway
              inputsWrite.println(values_static[2]);   // Subnet
              inputsWrite.println(values_static[3]);   // DNS
              inputsWrite.close();
              logOutput("Configuration saved !");
              request->send(200, "text/html", (String)"<div style=\"text-align:center; font-family:arial;\">Congratulation !</br></br>You have successfully changed the networks settings.</br></br>The device will now restart and try to apply the new settings.</br></br>Please wait 10 seconds and then press on the \"Go Home\" button to return to the main page.</br></br>If you can't return to the main page please check the entered values.</br></br><form method=\"post\" action=\"http://" + values_static[0] + "\"><input type=\"submit\" value=\"Go Home\"/></form></div>"); 
              shouldReboot = true;
        } else request->redirect("/staticIP");
      } else {
        request->redirect("/staticIP");
      }          
    }); // server.on("/staticLogin", HTTP_POST, [](AsyncWebServerRequest * request)

    server.on("/dhcpIP", HTTP_POST, [](AsyncWebServerRequest * request){
      if(request->hasArg("saveDHCP")){        
        File inputsWrite = SPIFFS.open("/networkRadar.txt", "w");
        if(!inputsWrite) logOutput((String)"ERROR_INSIDE_POST ! Couldn't open file to write DHCP IP credentials !");
        inputsWrite.println("DHCP IP");  // Connection Type ? WiFi : Ethernet
        inputsWrite.close();
        logOutput("Configuration saved !");
        request->send(200, "text/html", "<div style=\"text-align:center; font-family:arial;\">Congratulation !</br></br>You have successfully changed the networks settings.</br></br>The device will now restart and try to apply the new settings.</br></br>You can get this device's IP by looking through your Access Point's DHCP List."); 
        shouldReboot = true;
      } else {
        request->redirect("/dhcpIP");        
      }
    }); // server.on("/dhcpLogin", HTTP_POST, [](AsyncWebServerRequest * request)

    server.on("/radar", HTTP_POST, [](AsyncWebServerRequest * request){
      if(request->hasArg("save_values")){
        request->redirect("/radar");
        int params = request->params();
        for(int i=0;i<params;i++){
          AsyncWebParameter* p = request->getParam(i);
            if(p->isPost() && p->value() != NULL && p->name() != "save_values") {
              File configWrite = SPIFFS.open("/configRadar.txt", "w");
              if(!configWrite) logOutput((String)"ERROR: Couldn't open configRadar to save values from POST !");
                logOutput((String)"POST[" + p->name().c_str() + "]: " + p->value().c_str());
              if(p->name() == "getUnits" && p->value()!= "0") {
                Units = p->value();
                if(Units == "MPH") {
                  USE_SERIAL1.write("SET speedUnits 0\r\n");
                } else if(Units == "KPH") {
                  USE_SERIAL1.write("SET speedUnits 1\r\n");
                }
                configWrite.println(Server_IP);
                configWrite.println(Server_Port);
                configWrite.println(Units);
                configWrite.println(TriggerSpeed);
                configWrite.println(Direction);
                configWrite.println(Threshold);
              }
              if(p->name() == "getTrigger") {
                TriggerSpeed = p->value();
                String str_1 = "SET output1min " + TriggerSpeed + "\r\n";
                USE_SERIAL1.write(str_1.c_str());
                configWrite.println(Server_IP);
                configWrite.println(Server_Port);
                configWrite.println(Units);
                configWrite.println(TriggerSpeed);
                configWrite.println(Direction);
                configWrite.println(Threshold);
              }
              if(p->name() == "getDirection" && p->value()!= "0") {
                Direction = p->value();
                if(Direction == "Towards") {
                  USE_SERIAL1.write("SET detectionDirection 0\r\n");                  
                } else if(Direction == "Away") {
                  USE_SERIAL1.write("SET detectionDirection 1\r\n");
                } else if(Direction == "Bidirectional") {
                  USE_SERIAL1.write("SET detectionDirection 2\r\n");
                }
                configWrite.println(Server_IP);
                configWrite.println(Server_Port);
                configWrite.println(Units);
                configWrite.println(TriggerSpeed);
                configWrite.println(Direction);
                configWrite.println(Threshold);
              }
              if(p->name() == "getThreshold") {
                Threshold = p->value();
                String str_2 = "SET detectionThreshold " + Threshold + "\r\n";
                USE_SERIAL1.write(str_2.c_str());
                configWrite.println(Server_IP);
                configWrite.println(Server_Port);
                configWrite.println(Units);
                configWrite.println(TriggerSpeed);
                configWrite.println(Direction);
                configWrite.println(Threshold);
              }
                configWrite.close();
              }
        } // for(int i=0;i<params;i++)
      } else {
        request->redirect("/radar");
      }
    });

    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
      if(userFlag) {
        if(!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
          return request->requestAuthentication(NULL,false);            
        request->send(SPIFFS, "/update_page.html", "text/html", false, processor);
      } else {
        request->send(SPIFFS, "/update_page.html", "text/html", false, processor);
      }
    });

    server.onFileUpload(handleUpload);
    server.onNotFound([](AsyncWebServerRequest *request){ request->redirect("/home"); });
    server.begin();

    //Initialize TCP Server
    TCPserver.begin();

    if(SPIFFS.exists("/configRadar.txt")) {
      File configRead = SPIFFS.open("/configRadar.txt", "r");
      if(!configRead) logOutput((String)"Couldn't read /configRadar.txt");
      String values_config[6];      
      fileReadLines(configRead, values_config);
      configRead.close();
      if(values_config[0].length() != 0) {
        Server_IP = values_config[0];
      }
      if(values_config[1].length() != 0) {
        Server_Port = values_config[1];
      }
      if(values_config[2].length() != 0) {
        Units = values_config[2];
      }
      if(values_config[3].length() != 0) {
        TriggerSpeed = values_config[3];
      }
      if(values_config[4].length() != 0) {
        Direction = values_config[4];
      }
      if(values_config[5].length() != 0) {
        Threshold = values_config[5];
      }
      if(Units == "MPH") {
        USE_SERIAL1.write("SET speedUnits 0\r\n");
      } else if(Units == "KPH") {
        USE_SERIAL1.write("SET speedUnits 1\r\n");
      }
      String str_1 = "SET output1min " + TriggerSpeed + "\r\n";
      USE_SERIAL1.write(str_1.c_str());
      if(Direction == "Towards") {
        USE_SERIAL1.write("SET detectionDirection 0\r\n");                  
      } else if(Direction == "Away") {
        USE_SERIAL1.write("SET detectionDirection 1\r\n");
      } else if(Direction == "Bidirectional") {
        USE_SERIAL1.write("SET detectionDirection 2\r\n");
      }
      String str_2 = "SET detectionThreshold " + Threshold + "\r\n";
      USE_SERIAL1.write(str_2.c_str());
    }

  } else {
    networkRead.close();
    isAPmodeOn = true;
    logOutput(WiFi.mode(WIFI_AP) ? "Controller went in AP Mode !" : "ERROR: Controller couldn't go in AP_MODE. AP_STATION_MODE will start.");
    startAP();
  }

  USE_SERIAL1.write("SET output1HoldTime 500\r\n");
}

String Server_Port_Old = "";
String Radar_String = "";

unsigned int startTimeSerial = 0;
unsigned int deltaTimeSerial = 0;
bool trigger = false;

unsigned int startTimeTrigger = 0;
unsigned int deltaTimeTrigger = 0;
unsigned int triggerCounter = 0;


void foo(uint8_t *buff, int length) {
  for(int i=0; i<length;i++) {
    // Serial.print(buff[i]);
    printf("%c ", buff[i]);
    // Serial.print(" ");
  }
  Serial.println();
}

void loop() {
  if (shouldReboot) {
    logOutput("Restarting in 1 second...");
    delay(1000);
    server.reset();
    ESP.restart();
  }
  // TCPserver.

  if(!isAPmodeOn) {
    //TCP Connection
    WiFiClient client = TCPserver.available();

    // Initialize TCP Connection and send data
    if(client) {
      Serial.println((String)"Client IP Address: " + client.remoteIP().toString());
      Serial.println((String)"Client Port: " + client.remotePort());
      logOutput("Client has connected.");
      Radar_String = "";
      USE_SERIAL1.flush();
      while(client.connected()) {
        deltaTimeSerial = millis();
        if (shouldReboot) {
          logOutput("Restarting in 1 second...");
          delay(1000);
          server.reset();
          ESP.restart();
        }

        // Check if we are receiving serial data from Radar
        if(USE_SERIAL1.available() > 0) {
          while (USE_SERIAL1.available()) {
            char radar = USE_SERIAL1.read();
            Radar_String += radar; // this adds up all the input
          }
          startTimeSerial = millis();
        }

        if(Radar_String.length()!=0 && Radar_String.indexOf("SET") < 0) {
          client.write(Radar_String.c_str());
          Radar_String = "";
        } else if(Radar_String.indexOf("SET")>0 && Radar_String.indexOf("OK")>0) {
          logOutput("Settings successfully applied.");
          Radar_String = "";
        } else if((deltaTimeSerial - startTimeSerial) > 1000) {
          client.write("0\r\n");
          startTimeSerial = millis();
          Radar_String = "";
        } else {
          Radar_String = "";
        }

        //Check for SERVER's PORT and initializes UDP
        if(Server_Port_Old != Server_Port && Server_Port != "0000") {
          Server_Port_Old = Server_Port;
          Serial.println("New Server Port: " + Server_Port_Old);
          udp.stop();
          delay(100);
          udp.begin(Server_Port.toInt());
        }

        //Send UDP packet if trigger was sent from Radar        
        if(digitalRead(TRIGGER_PIN) == LOW && trigger == false) {
          trigger = true;
          logOutput("Trigger sent");
          uint8_t buffer[19] = "statechange,201,1\r";
          // send packet to server
          if (Server_IP.length()!=0) {
            udp.beginPacket(Server_IP.c_str(), Server_Port.toInt());            
            udp.write(buffer, sizeof(buffer));            
            delay(30);
            Serial.println(udp.endPacket());
            memset(buffer, 0, 19);            
          } else {
            logOutput("ERROR ! No IP for the Server was found. Please enter Server's IP !");
          }
          delay(520);
        } //if(digitalRead(TRIGGER_PIN) == LOW) {
        
        if(digitalRead(TRIGGER_PIN) == HIGH && trigger == true) {
          trigger = false;
        }
        delay(2);
      } // while(client.connected()) {
      client.stop();
      logOutput("Client has disconnected.");
    } // if(client) {

  } else {
    delay(2);
  } // if(!isAPmodeOn)
  delay(2);
}