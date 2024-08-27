#include <DNSServer.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include "ESPAsyncWebSrv.h"

#include "FS.h"
#include <LittleFS.h>

#include "webpages.h"

DNSServer dnsServer;
AsyncWebServer server(80);

float result = 0.0;



// list all of the files, if ishtml=true, return html rather than simple text
String listFiles(bool ishtml = false) {
  String returnText = "";
  Serial.println("Listing files stored on LittleFS");
  File root = LittleFS.open("/");
  File foundfile = root.openNextFile();
  if (ishtml) {
    returnText += "<table><tr><th align='left'>Name</th><th align='left'>Size</th><th></th><th></th></tr>";
  }
  while (foundfile) {
    if (ishtml) {
      returnText += "<tr align='left'><td><a href=\'/" + String(foundfile.name()) + "\'>/" + String(foundfile.name()) + "</a></td><td align='right'>" + humanReadableSize(foundfile.size()) + "</td>";
      returnText += "<td><button onclick=\"downloadDeleteButton(\'/" + String(foundfile.name()) + "\', \'download\')\">Download</button>";
      returnText += "<td><button onclick=\"downloadDeleteButton(\'/" + String(foundfile.name()) + "\', \'delete\')\">Delete</button></tr>";
    } else {
      returnText += "File: /" + String(foundfile.name()) + " Size: " + humanReadableSize(foundfile.size()) + "\n";
    }
    foundfile = root.openNextFile();
  }
  if (ishtml) {
    returnText += "<tr><td colspan=2><hr></td></tr>";
    returnText += "<tr align='right'><td><i>Used<\i></td><td><i>" + humanReadableSize(LittleFS.usedBytes()) + "<\i></td><td></td><td></td>";
    returnText += "<tr align='right'><td><i>Free<\i></td><td><i>" + humanReadableSize(LittleFS.totalBytes() - LittleFS.usedBytes()) + "<\i></td><td></td><td></td>";
    returnText += "<tr><td colspan=2><hr></td></tr>";
    returnText += "<tr align='right'><td><i>Total<\i></td><td><i>" + humanReadableSize(LittleFS.totalBytes()) + "<\i></td><td></td><td></td>";
    returnText += "</table>";
  }
  root.close();
  foundfile.close();
  return returnText;
}

// Make size of files human readable
// source: https://github.com/CelliesProjects/minimalUploadAuthESP32
String humanReadableSize(const size_t bytes) {
  if (bytes < 1024) return String(bytes) + " B";
  else if (bytes < (1024 * 1024)) return String(bytes / 1024.0) + " kB";
  else if (bytes < (1024 * 1024 * 1024)) return String(bytes / 1024.0 / 1024.0) + " MB";
  else return String(bytes / 1024.0 / 1024.0 / 1024.0) + " GB";
}

// web content template processor
String processor(const String& var)
{
  if(var == "RESULT")
    return String(result);
  return String();
}

// handles uploads
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  Serial.println("Client:" + request->client()->remoteIP().toString() + " " + request->url());

  if (!index) {
    // open the file on first call and store the file handle in the request object
    request->_tempFile = LittleFS.open("/" + filename, "w");
    Serial.println("Upload Start: " + String(filename));
  }

  if (len && request->_tempFile) {
    // stream the incoming chunk to the opened file
    size_t bytesWritten = request->_tempFile.write(data, len);
    Serial.print("Writing file: " + String(filename) + " index=" + String(index) + " len=" + String(len));
    if (bytesWritten != len) {
      // close the file handle and remove it as the upload is aborted
      request->_tempFile.close();
      LittleFS.remove("/" + filename);
      Serial.print(" FAILED");
      request->send(422, "text/plain", "ERROR: write operation on LittelFS failed - file probably too large");
    }
    Serial.println();
  }

  if (final) {
    if (request->_tempFile) {
      // close the file handle as the upload is now done
      request->_tempFile.close();
      Serial.println("Upload Complete: " + String(filename) + ",size: " + String(index + len));
      // request->redirect("/");
    } else {
      Serial.println("Upload aborted: " + String(filename));
      // request->send(422, "text/plain", "ERROR: write operation on LittelFS failed - file probably too large");
      // request->redirect("/");
    }
  }
}

void logRq(AsyncWebServerRequest *request) {
  Serial.println("Client:" + request->client()->remoteIP().toString() + " " + request->url());
}

void setupServerCaptivePortal() {
  // from https://github.com/CDFER/Captive-Portal-ESP32/blob/main/src/main.cpp
	//======================== Webserver ========================
	// WARNING IOS (and maybe macos) WILL NOT POP UP IF IT CONTAINS THE WORD "Success" https://www.esp8266.com/viewtopic.php?f=34&t=4398
	// SAFARI (IOS) IS STUPID, G-ZIPPED FILES CAN'T END IN .GZ https://github.com/homieiot/homie-esp8266/issues/476 this is fixed by the webserver serve static function.
	// SAFARI (IOS) there is a 128KB limit to the size of the HTML. The HTML can reference external resources/images that bring the total over 128KB
	// SAFARI (IOS) popup browser has some severe limitations (javascript disabled, cookies disabled)

	// Required
	server.on("/connecttest.txt", [](AsyncWebServerRequest *request) { logRq(request); request->redirect("http://logout.net"); });	// windows 11 captive portal workaround
	server.on("/wpad.dat", [](AsyncWebServerRequest *request) { logRq(request); request->send(404); });								// Honestly don't understand what this is but a 404 stops win 10 keep calling this repeatedly and panicking the esp32 :)

	// Background responses: Probably not all are Required, but some are. Others might speed things up?
	// A Tier (commonly used by modern systems)
	server.on("/generate_204", [](AsyncWebServerRequest *request) { logRq(request); request->redirect("/"); });		   // android captive portal redirect
	server.on("/redirect", [](AsyncWebServerRequest *request) { logRq(request); request->redirect("/"); });			   // microsoft redirect
	server.on("/hotspot-detect.html", [](AsyncWebServerRequest *request) { logRq(request); request->redirect("/"); });  // apple call home
	server.on("/canonical.html", [](AsyncWebServerRequest *request) { logRq(request); request->redirect("/"); });	   // firefox captive portal call home
	server.on("/success.txt", [](AsyncWebServerRequest *request) { logRq(request); request->send(200); });					   // firefox captive portal call home
	server.on("/ncsi.txt", [](AsyncWebServerRequest *request) { logRq(request); request->redirect("/"); });			   // windows call home

	// B Tier (uncommon)
	 server.on("/chrome-variations/seed",[](AsyncWebServerRequest *request) { logRq(request); request->send(200); }); //chrome captive portal call home
	 server.on("/service/update2/json",[](AsyncWebServerRequest *request) { logRq(request); request->send(200); }); //firefox?
	 server.on("/chat",[](AsyncWebServerRequest *request) { logRq(request); request->send(404); }); //No stop asking Whatsapp, there is no internet connection
	 server.on("/startpage",[](AsyncWebServerRequest *request) { logRq(request); request->redirect("/"); });

	// return 404 to webpage icon
	server.on("/favicon.ico", [](AsyncWebServerRequest *request) { logRq(request); request->send(404); });	// webpage icon
}

void setupServer() {
  setupServerCaptivePortal();
  server.on("/admin/fs", HTTP_GET, [](AsyncWebServerRequest * request) {
    logRq(request);

    request->send(200, "text/html", fs_html);
  });

  server.on("/admin/listfiles", HTTP_GET, [](AsyncWebServerRequest * request) {
    logRq(request);

    request->send(200, "text/plain", listFiles(true));
  });

  server.on("/admin/file", HTTP_GET, [](AsyncWebServerRequest * request) {
    logRq(request);

    if (request->hasParam("name") && request->hasParam("action")) {
      const String &fileName = request->getParam("name")->value();
      const String &fileAction = request->getParam("action")->value();

      String logmessage = "Client:" + request->client()->remoteIP().toString() + " " + request->url() + "?name=" + fileName + "&action=" + fileAction;

      if (!LittleFS.exists(fileName)) {
        Serial.println(logmessage + " ERROR: file does not exist");
        request->send(400, "text/plain", "ERROR: file does not exist");
      } else {
        Serial.println(logmessage + " file exists");
        if (fileAction.equals("download")) {
          logmessage += " downloaded";
          request->send(LittleFS, fileName, "application/octet-stream", true);
        } else if (fileAction.equals("delete")) {
          logmessage += " deleted";
          LittleFS.remove(fileName);
          request->send(200, "text/plain", "Deleted File: " + fileName);
        } else {
          logmessage += " ERROR: invalid action param supplied";
          request->send(400, "text/plain", "ERROR: invalid action param supplied");
        }
        Serial.println(logmessage);
      }
    } else {
      request->send(400, "text/plain", "ERROR: name and action params required");
    }
  });

  server.on("/admin/sysinfo", HTTP_GET, [] (AsyncWebServerRequest *request) {
    logRq(request);

    String result;

    result += "<table><tr><th align='left'>Attribute</th><th align='left'>Value</th></tr>";
    result += "<tr align='left'><td>Chip Model</td><td>" + String(ESP.getChipModel()) + "</td></tr>";
    result += "<tr align='left'><td>Chip Cores</td><td>" + String(ESP.getChipCores()) + "</td></tr>";
    result += "<tr align='left'><td>Chip Revision</td><td>" + String(ESP.getChipRevision()) + "</td></tr>";
    result += "<tr align='left'><td>Flash Size</td><td>" + humanReadableSize(ESP.getFlashChipSize()) + "</td></tr>";
    result += "<tr align='left'><td>Free Heap</td><td>" + humanReadableSize(ESP.getFreeHeap()) + "</td></tr>";
    result += "<tr align='left'><td>File System Free</td><td>" + humanReadableSize((LittleFS.totalBytes() - LittleFS.usedBytes())) + "</td></tr>";
    result += "<tr align='left'><td>File System Used</td><td>" + humanReadableSize(LittleFS.usedBytes()) + "</td></tr>";
    result += "<tr align='left'><td>File System Size</td><td>" + humanReadableSize(LittleFS.totalBytes()) + "</td></tr>";
    result += "</table>";

    request->send(200, "text/plain", result);
  });

  server.onFileUpload(handleUpload);

  server.on("/*", HTTP_GET, [](AsyncWebServerRequest *request) {
    logRq(request);

    String uri = request->url();
    if (uri.equals("/")) uri = "/index.html";

    if (request->hasParam("lOperand") && request->hasParam("rOperand") && request->hasParam("operator")) {
      const String &_lOperand_string = request->getParam("lOperand")->value();
      const float _lOperand = _lOperand_string.length() == 0 ? result : _lOperand_string.toFloat();
      const float _rOperand = request->getParam("rOperand")->value().toFloat();
      const String &_operator = request->getParam("operator")->value();

      if (_operator.equals("add")) {
        result = _lOperand + _rOperand;
      } else if (_operator.equals("sub")) {
        result = _lOperand - _rOperand;
      } else if (_operator.equals("mul")) {
        result = _lOperand * _rOperand;
      } else if (_operator.equals("div")) {
        result = _lOperand / _rOperand;
      }
    }
    request->send(LittleFS, uri, "", false, processor); // content type automagically determined by file extension
  });
    
  server.onNotFound([](AsyncWebServerRequest *request) {
    logRq(request);

    Serial.printf("NOT_FOUND: ");
    if(request->method() == HTTP_GET){
      Serial.printf("GET");
//      request->redirect("/");
    }else if(request->method() == HTTP_POST)
      Serial.printf("POST");
    else if(request->method() == HTTP_DELETE)
      Serial.printf("DELETE");
    else if(request->method() == HTTP_PUT)
      Serial.printf("PUT");
    else if(request->method() == HTTP_PATCH)
      Serial.printf("PATCH");
    else if(request->method() == HTTP_HEAD)
      Serial.printf("HEAD");
    else if(request->method() == HTTP_OPTIONS)
      Serial.printf("OPTIONS");
    else
      Serial.printf("UNKNOWN");
    Serial.printf(" http://%s%s\n", request->host().c_str(), request->url().c_str());

    if(request->contentLength()){
      Serial.printf("_CONTENT_TYPE: %s\n", request->contentType().c_str());
      Serial.printf("_CONTENT_LENGTH: %u\n", request->contentLength());
    }

    int headers = request->headers();
    int i;
    for(i=0;i<headers;i++){
      AsyncWebHeader* h = request->getHeader(i);
      Serial.printf("_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
    }

    int params = request->params();
    for(i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isFile()){
        Serial.printf("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
      } else if(p->isPost()){
        Serial.printf("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
      } else {
        Serial.printf("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
      }
    }

    request->send(404);
  });
}


void setup() {
  //your other setup stuff...
  Serial.begin(115200);

  Serial.println("Mounting the filesystem...");
  if (!LittleFS.begin()) {
    Serial.println("could not mount the filesystem...");
    delay(2000);
    Serial.println("formatting...");
    LittleFS.format();
    delay(2000);
    Serial.println("restart.");
    delay(2000);
    ESP.restart();
  }
  
  //List contents of FS
  Serial.println(listFiles());

  Serial.println("Setting up AP Mode");
  WiFi.mode(WIFI_AP); 
  WiFi.softAP("MPLculator");
  Serial.print("AP IP address: ");Serial.println(WiFi.softAPIP());

  Serial.println("Setting up Async WebServer");
  setupServer();

  Serial.println("Starting DNS Server");
  dnsServer.setTTL(3600);
  dnsServer.start(53, "*", WiFi.softAPIP());

  server.begin();
  Serial.println("All Done!");
}

void loop() {
  dnsServer.processNextRequest();
  delay(30);
}
