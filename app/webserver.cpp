/**
 * @file
 * @author  Patrick Jahns http://github.com/patrickjahns
 *
 * @section LICENSE
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details at
 * https://www.gnu.org/copyleft/gpl.html
 *
 * @section DESCRIPTION
 *
 *
 */
#include <RGBWWCtrl.h>
#include <Data/WebHelpers/base64.h> 
#include <FlashString/Map.hpp>
#include <FlashString/Stream.hpp>
#include <Network/Http/Websocket/WebsocketResource.h>
#include <Storage.h>

#define NOCACHE
#define DEBUG_OBJECT_API

ApplicationWebserver::ApplicationWebserver() {
    _running = false;
    // keep some heap space free
    // value is a good guess and tested to not crash when issuing multiple parallel requests
    HttpServerSettings settings;
    settings.maxActiveConnections=40;
    settings.minHeapSize = _minimumHeapAccept;  
    settings.keepAliveSeconds = 10; // do not close instantly when no transmission occurs. some clients are a bit slow (like FHEM)
    configure(settings);

    // workaround for bug in Sming 3.5.0
    // https://github.com/SmingHub/Sming/issues/1236
    setBodyParser("*", bodyToStringParser);
}

void ApplicationWebserver::init() {
    paths.setDefault(HttpPathDelegate(&ApplicationWebserver::onFile, this));
    paths.set("/", HttpPathDelegate(&ApplicationWebserver::onIndex, this));
    paths.set("/webapp", HttpPathDelegate(&ApplicationWebserver::onWebapp, this));
    paths.set("/config", HttpPathDelegate(&ApplicationWebserver::onConfig, this));
    paths.set("/info", HttpPathDelegate(&ApplicationWebserver::onInfo, this));
    paths.set("/color", HttpPathDelegate(&ApplicationWebserver::onColor, this));
    paths.set("/networks", HttpPathDelegate(&ApplicationWebserver::onNetworks, this));
    paths.set("/scan_networks", HttpPathDelegate(&ApplicationWebserver::onScanNetworks, this));
    paths.set("/system", HttpPathDelegate(&ApplicationWebserver::onSystemReq, this));
    paths.set("/update", HttpPathDelegate(&ApplicationWebserver::onUpdate, this));
    paths.set("/connect", HttpPathDelegate(&ApplicationWebserver::onConnect, this));
    paths.set("/ping", HttpPathDelegate(&ApplicationWebserver::onPing, this));
    paths.set("/hosts", HttpPathDelegate(&ApplicationWebserver::onHosts, this));
    paths.set("/object", HttpPathDelegate(&ApplicationWebserver::onObject, this));
    // animation controls
    paths.set("/stop", HttpPathDelegate(&ApplicationWebserver::onStop, this));
    paths.set("/skip", HttpPathDelegate(&ApplicationWebserver::onSkip, this));
    paths.set("/pause", HttpPathDelegate(&ApplicationWebserver::onPause, this));
    paths.set("/continue", HttpPathDelegate(&ApplicationWebserver::onContinue, this));
    paths.set("/blink", HttpPathDelegate(&ApplicationWebserver::onBlink, this));

    paths.set("/toggle", HttpPathDelegate(&ApplicationWebserver::onToggle, this));

    // storage api
    paths.set("/storage",HttpPathDelegate(&ApplicationWebserver::onStorage, this));

    // websocket api
    wsResource= new WebsocketResource();
	wsResource->setConnectionHandler([this](WebsocketConnection& socket) { this->wsConnected(socket); });
    wsResource->setDisconnectionHandler([this](WebsocketConnection& socket) { this->wsDisconnected(socket); });
	paths.set("/ws", wsResource);

    _init = true;
}

void ApplicationWebserver::wsConnected(WebsocketConnection& socket){
    debug_i("===>wsConnected");
    webSockets.addElement(&socket);
    debug_i("===>nr of websockets: %i", webSockets.size());
}

void ApplicationWebserver::wsDisconnected(WebsocketConnection& socket){
    debug_i("<===wsDisconnected");
    webSockets.removeElement(&socket);
    debug_i("===>nr of websockets: %i", webSockets.size());
}

void ApplicationWebserver::wsBroadcast(String message){
    HttpConnection *connection = nullptr;
    String remoteIP;
    auto tcpConnections=getConnections();
    debug_i("=== Websocket Broadcast ===\n%s",message.c_str());
    debug_i("===>nr of tcpConnections: %i", tcpConnections.size());
    for(auto& connection : tcpConnections) { // Iterate over all active sockets
        remoteIP=String(connection->getRemoteIp().toString());
        debug_i("====> remote: %s",remoteIP.c_str());
    }
    debug_i("=========================================");
    debug_i("===>nr of websockets: %i", webSockets.size());
    for(auto& socket : webSockets) { // Iterate over all active sockets
        connection=socket->getConnection();
        remoteIP=String(connection->getRemoteIp().toString());
        debug_i("====> sending to socket %s",remoteIP.c_str());
        socket->send(message, WS_FRAME_TEXT); // Send the message to each socket
    }
}

void ApplicationWebserver::start() {
    if (_init == false) {
        init();
    }
    listen(80);
    _running = true;
}

void ApplicationWebserver::stop() {
    close();
    _running = false;
}

bool ICACHE_FLASH_ATTR ApplicationWebserver::authenticateExec(HttpRequest &request, HttpResponse &response) {
    if (!app.cfg.general.api_secured)
        return true;

    debug_d("ApplicationWebserver::authenticated - checking...");

    String userPass = request.getHeader("Authorization");
    if (userPass == String::nullstr) {
        debug_d("ApplicationWebserver::authenticated - No auth header");
        return false; // header missing
    }

    debug_d("ApplicationWebserver::authenticated Auth header: %s", userPass.c_str());

    // header in form of: "Basic MTIzNDU2OmFiY2RlZmc="so the 6 is to get to beginning of 64 encoded string
    userPass = userPass.substring(6); //cut "Basic " from start
    if (userPass.length() > 50) {
        return false;
    }

    userPass = base64_decode(userPass);
    debug_d("ApplicationWebserver::authenticated Password: '%s' - Expected password: '%s'", userPass.c_str(), app.cfg.general.api_password.c_str());
    if (userPass.endsWith(app.cfg.general.api_password)) {
        return true;
    }

    return false;
}

bool ICACHE_FLASH_ATTR ApplicationWebserver::authenticated(HttpRequest &request, HttpResponse &response) {
    bool authenticated = authenticateExec(request, response);

    if (!authenticated) {
        response.code = HTTP_STATUS_UNAUTHORIZED;
        response.setHeader("WWW-Authenticate", "Basic realm=\"RGBWW Server\"");
        response.setHeader("401 wrong credentials", "wrong credentials");
        response.setHeader("Connection", "close");
    }

    return authenticated;
}

String ApplicationWebserver::getApiCodeMsg(API_CODES code) {
    switch (code) {
    case API_CODES::API_MISSING_PARAM:
        return String("missing param");
    case API_CODES::API_UNAUTHORIZED:
        return String("authorization required");
    case API_CODES::API_UPDATE_IN_PROGRESS:
        return String("update in progress");
    default:
        return String("bad request");
    }
}

void ApplicationWebserver::sendApiResponse(HttpResponse &response, JsonObjectStream* stream, HttpStatus code) {
    if (!checkHeap(response)) {
        delete stream;
        return;
    }

    response.setAllowCrossDomainOrigin("*");
    response.setHeader("accept","GET, POST, OPTIONS");
    response.setHeader("Access-Control-Allow-Headers","*");
    if (code != HTTP_STATUS_OK) {
        response.code = HTTP_STATUS_BAD_REQUEST;
    }
    response.sendDataStream(stream, MIME_JSON);
}

void ApplicationWebserver::sendApiCode(HttpResponse &response, API_CODES code, String msg /* = "" */) {
    JsonObjectStream* stream = new JsonObjectStream();
    JsonObject json = stream->getRoot();
    if (msg == "") {
        msg = getApiCodeMsg(code);
    }
    if (code == API_CODES::API_SUCCESS) {
        json["success"] = true;
        sendApiResponse(response, stream, HTTP_STATUS_OK);
    } else {
        json["error"] = msg;
        sendApiResponse(response, stream, HTTP_STATUS_BAD_REQUEST);
    }
}

void ApplicationWebserver::onFile(HttpRequest &request, HttpResponse &response) {

    if (!authenticated(request, response)) {
        return;
    }

#ifdef ARCH_ESP8266
    if (app.ota.isProccessing()) {
        response.setContentType(MIME_TEXT);
        response.code = HTTP_STATUS_SERVICE_UNAVAILABLE;
        response.sendString("OTA in progress");
        return;
    }
#endif
	// Use client caching for better performance.
	#ifndef NOCACHE
    	response->setCache(86400, true);
    #endif
    
    if (!app.isFilesystemMounted()) {
        response.setContentType(MIME_TEXT);
        response.code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        response.sendString("No filesystem mounted");
        return;
    }

    String file = request.uri.Path;
    if (file[0] == '/')
        file = file.substring(1);
    if (file[0] == '.') {
        response.code = HTTP_STATUS_FORBIDDEN;
        return;
    }

    if (!fileExist(file) && !fileExist(file + ".gz") && WifiAccessPoint.isEnabled()) {
        //if accesspoint is active and we couldn`t find the file - redirect to index
        debug_d("ApplicationWebserver::onFile redirecting");
        response.headers[HTTP_HEADER_LOCATION] = "http://" + WifiAccessPoint.getIP().toString() +"/";
    } else {
        #ifndef NOCACHE
        response.setCache(86400, true); // It's important to use cache for better performance.
        #endif
        response.code=HTTP_STATUS_OK;
        response.sendFile(file);
    }

}
void ApplicationWebserver::onWebapp(HttpRequest &request, HttpResponse &response) {
    if (!authenticated(request, response)) {
        return;
    }

    response.headers[HTTP_HEADER_LOCATION]="/index.html";
    response.setHeader("Access-Control-Allow-Origin", "*");

    response.code = HTTP_STATUS_PERMANENT_REDIRECT;
    response.sendString("Redirecting to /index.html");
}

void ApplicationWebserver::onIndex(HttpRequest &request, HttpResponse &response) {
    debug_i("http onIndex");
    if (!authenticated(request, response)) {
        return;
    }

#ifdef ARCH_ESP8266
    if (app.ota.isProccessing()) {
        response.setContentType(MIME_TEXT);
        response.code = HTTP_STATUS_SERVICE_UNAVAILABLE;
        response.sendString("OTA in progress");
        return;	void publishTransitionFinished(const String& name, bool requeued = false);
    }
#endif

    if (request.method != HTTP_GET) {
        response.code = HTTP_STATUS_BAD_REQUEST;
        return;
    }

    if (request.method == HTTP_OPTIONS){
        // probably a CORS request
        response.setHeader("Access-Control-Allow-Origin", "*");
        sendApiCode(response,API_CODES::API_SUCCESS,"");
        debug_i("HTTP_OPTIONS Request, sent API_SUCCSSS");
        return;
    }

    if (!app.isFilesystemMounted()) {
        response.setContentType(MIME_TEXT);
        response.code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        response.sendString("No filesystem mounted");
        return;
    }
    
    /* removing the init redirect as this is now handled in the front end component
    if (WifiAccessPoint.isEnabled()&&!WifiStation.isConnected()&&request.getQueryParameter("init")!="true") {
        // not yet connected - redirect to initial settings page
        debug_i("activating query parameter");
        response.headers[HTTP_HEADER_LOCATION]="http://" + WifiAccessPoint.getIP().toString()+"/?init=true";
        response.code = HTTP_STATUS_PERMANENT_REDIRECT;

    } else if(WifiStation.isConnected()&&(request.getQueryParameter("init")=="true")) {
        // the controller is connected to a wifi network and the init parameter is set, needs clearing
        // this should also redirect the browser if it has just been used to configure the wifi and now
        // reconnected through the AP to the controller. The redirect points to the Station IP address
        // so if that is reachable (depending on the client's connection), this should go directly to the app
        debug_i("deactivating query parameter");
        response.headers[HTTP_HEADER_LOCATION]="http://" + WifiStation.getIP().toString()+"/";
        response.code = HTTP_STATUS_PERMANENT_REDIRECT;
    }
    // if neither of the two redirect cases is true, serve the index.html file
    else {
    */
        // we are connected to ap - serve normal settings page
        response.setHeader("Access-Control-Allow-Origin", "*");
        response.sendFile("index.html");
    /*}
    */
}

bool ApplicationWebserver::checkHeap(HttpResponse &response) {
    unsigned fh = system_get_free_heap_size();
    if (fh < _minimumHeap) {
        response.code = HTTP_STATUS_TOO_MANY_REQUESTS;
        response.setHeader("Retry-After", "2");
        return false;
    }
    return true;
}

void ApplicationWebserver::onConfig(HttpRequest &request, HttpResponse &response) {
    debug_i("onConfig");
    if (!checkHeap(response))
        return;

    if (!authenticated(request, response)) {
        return;
    }

#ifdef ARCH_ESP8266
    if (app.ota.isProccessing()) {
        sendApiCode(response, API_CODES::API_UPDATE_IN_PROGRESS);
        return;
    }
#endif
    

    if (request.method != HTTP_POST && request.method != HTTP_GET && request.method!=HTTP_OPTIONS) {
        sendApiCode(response, API_CODES::API_BAD_REQUEST, "not POST, GET or OPTIONS request");
        return;
    }
    
    /*
    / handle HTTP_OPTIONS request to check if server is CORS permissive (which this firmware 
    / has been for years) this is just to reply to that request in order to pass the CORS test
    */
    if (request.method == HTTP_OPTIONS){
        // probably a CORS request
        response.setHeader("Access-Control-Allow-Origin", "*");
        sendApiCode(response,API_CODES::API_SUCCESS,"");
        debug_i("HTTP_OPTIONS Request, sent API_SUCCSSS");
        return;
    }
    
    if (request.method == HTTP_POST) {
        debug_i("======================\nHTTP POST request received, ");
        String body = request.getBody();
        debug_i("body: \n", body);
        if (body == NULL) {

            sendApiCode(response, API_CODES::API_BAD_REQUEST, "could not parse HTTP body");
            return;
        }

        bool error = false;
        String error_msg = getApiCodeMsg(API_CODES::API_BAD_REQUEST);
        DynamicJsonDocument doc(CONFIG_MAX_LENGTH); //TODO: CONFIG_MAX_LENGTH is 2048, that would not fit into one package, not sure what happens then
        Json::deserialize(doc, body);

        // remove comment for debugging
        //Json::serialize(doc, Serial, Json::Pretty);

        bool ip_updated = false;
        bool color_updated = false;
        bool ap_updated = false;
        JsonObject root = doc.as<JsonObject>();
        if (root.isNull()) {
            sendApiCode(response, API_CODES::API_BAD_REQUEST, "no root object");
            return;
        }

        JsonObject jnet = root["network"];
        if (!jnet.isNull()) {
  
            JsonObject con = jnet["connection"];
            if (!con.isNull()) {
                ip_updated |= Json::getBoolTolerantChanged(con["dhcp"], app.cfg.network.connection.dhcp);

                if (!app.cfg.network.connection.dhcp) {
                    //only change if dhcp is off - otherwise ignore
                    IpAddress ip, netmask, gateway;
                    const char* str;
                    if (Json::getValue(con["ip"], str)) {
                    	ip = str;
                        if (!(ip == app.cfg.network.connection.ip)) {
                            app.cfg.network.connection.ip = ip;
                            ip_updated = true;
                        }
                    } else {
                        error = true;
                        error_msg = "missing ip";
                    }
                    if (Json::getValue(con["netmask"], str)) {
                        netmask = str;
                        if (!(netmask == app.cfg.network.connection.netmask)) {
                            app.cfg.network.connection.netmask = netmask;
                            ip_updated = true;
                        }
                    } else {
                        error = true;
                        error_msg = "missing netmask";
                    }
                    if (Json::getValue(con["gateway"], str)) {
                        gateway = str;
                        if (!(gateway == app.cfg.network.connection.gateway)) {
                            app.cfg.network.connection.gateway = gateway;
                            ip_updated = true;
                        }
                    } else {
                        error = true;
                        error_msg = "missing gateway";
                    }

                }

            }
            if (!jnet["ap"].isNull()) {

            	String ssid;
            	ap_updated |= Json::getValueChanged(jnet["ap"]["ssid"], app.cfg.network.ap.ssid);

            	bool secured;
                if (Json::getBoolTolerant(jnet["ap"]["secured"], secured)) {
                    if (secured) {
                        if (Json::getValueChanged(jnet["ap"]["password"], app.cfg.network.ap.password)) {
							app.cfg.network.ap.secured = true;
							ap_updated = true;
                        } else {
                            error = true;
                            error_msg = "missing password for securing ap";
                        }
                    } else if (secured != app.cfg.network.ap.secured) {
                        app.cfg.network.ap.secured = secured;
                        ap_updated = true;
                    }
                }

            }

            JsonObject jmqtt = jnet["mqtt"];
            if (!jmqtt.isNull()) {
                //TODO: what to do if changed?
            	Json::getBoolTolerant(jmqtt["enabled"], app.cfg.network.mqtt.enabled);
            	Json::getValue(jmqtt["server"], app.cfg.network.mqtt.server);
            	Json::getValue(jmqtt["port"], app.cfg.network.mqtt.port);
            	Json::getValue(jmqtt["username"], app.cfg.network.mqtt.username);
            	Json::getValue(jmqtt["password"], app.cfg.network.mqtt.password);
            	Json::getValue(jmqtt["topic_base"], app.cfg.network.mqtt.topic_base);
            }
        }

        JsonObject jcol = root["color"];
        if (!jcol.isNull()) {

        	JsonObject jhsv = jcol["hsv"];
            if (!jhsv.isNull()) {
            	color_updated |= Json::getValueChanged(jhsv["model"], app.cfg.color.hsv.model);
            	color_updated |= Json::getValueChanged(jhsv["red"], app.cfg.color.hsv.red);
            	color_updated |= Json::getValueChanged(jhsv["yellow"], app.cfg.color.hsv.yellow);
            	color_updated |= Json::getValueChanged(jhsv["green"], app.cfg.color.hsv.green);
            	color_updated |= Json::getValueChanged(jhsv["cyan"], app.cfg.color.hsv.cyan);
            	color_updated |= Json::getValueChanged(jhsv["blue"], app.cfg.color.hsv.blue);
            	color_updated |= Json::getValueChanged(jhsv["magenta"], app.cfg.color.hsv.magenta);
            }
        	color_updated |= Json::getValueChanged(jcol["outputmode"], app.cfg.color.outputmode);
        	Json::getValue(jcol["startup_color"], app.cfg.color.startup_color);

        	JsonObject jbri = jcol["brightness"];
        	if (!jbri.isNull()) {
        		color_updated |= Json::getValueChanged(jbri["red"], app.cfg.color.brightness.red);
        		color_updated |= Json::getValueChanged(jbri["green"], app.cfg.color.brightness.green);
        		color_updated |= Json::getValueChanged(jbri["blue"], app.cfg.color.brightness.blue);
        		color_updated |= Json::getValueChanged(jbri["ww"], app.cfg.color.brightness.ww);
        		color_updated |= Json::getValueChanged(jbri["cw"], app.cfg.color.brightness.cw);
            }

        	JsonObject jcoltemp = jcol["colortemp"];
        	if (!jcoltemp.isNull()) {
        		color_updated |= Json::getValueChanged(jcoltemp["ww"], app.cfg.color.colortemp.ww);
        		color_updated |= Json::getValueChanged(jcoltemp["cw"], app.cfg.color.colortemp.cw);
            }
        }

        JsonObject jsec = root["security"];
        if (!jsec.isNull()) {
        	bool secured;
        	if (Json::getBoolTolerant(jsec["api_secured"], secured)) {
                if (secured) {
                    if (Json::getValue(jsec["api_password"], app.cfg.general.api_password)) {
                        app.cfg.general.api_secured = secured;
                    } else {
                        error = true;
                        error_msg = "missing password to secure settings";
                    }
                } else {
                    app.cfg.general.api_secured = false;
                    app.cfg.general.api_password = nullptr;
                }

            }
        }

        Json::getValue(root["ota"]["url"], app.cfg.general.otaurl);

        JsonObject jgen = root["general"];
        if (!jgen.isNull()) {
            debug_i("general settings found");
        	Json::getValue(jgen["device_name"], app.cfg.general.device_name);
        	debug_i("device_name: %s", app.cfg.general.device_name.c_str());
            Json::getValue(jgen["pin_config"], app.cfg.general.pin_config);
        	Json::getValue(jgen["buttons_config"], app.cfg.general.buttons_config);
        	Json::getValue(jgen["buttons_debounce_ms"], app.cfg.general.buttons_debounce_ms);
        }

        JsonObject jntp = root["ntp"];
        if (!jntp.isNull()) {
            Json::getBoolTolerant(jntp["enabled"], app.cfg.ntp.enabled);
            Json::getValue(jntp["server"], app.cfg.ntp.server);
            Json::getValue(jntp["interval"], app.cfg.ntp.interval);
        }

        JsonObject jsync = root["sync"];
        if (!jsync.isNull()) {
            Json::getBoolTolerant(jsync["clock_master_enabled"], app.cfg.sync.clock_master_enabled);
        	Json::getValue(jsync["clock_master_interval"], app.cfg.sync.clock_master_interval);
        	Json::getBoolTolerant(jsync["clock_slave_enabled"], app.cfg.sync.clock_slave_enabled);
        	Json::getValue(jsync["clock_slave_topic"], app.cfg.sync.clock_slave_topic);
        	Json::getBoolTolerant(jsync["cmd_master_enabled"], app.cfg.sync.cmd_master_enabled);
        	Json::getBoolTolerant(jsync["cmd_slave_enabled"], app.cfg.sync.cmd_slave_enabled);
        	Json::getValue(jsync["cmd_slave_topic"], app.cfg.sync.cmd_slave_topic);

        	Json::getBoolTolerant(jsync["color_master_enabled"], app.cfg.sync.color_master_enabled);
        	Json::getValue(jsync["color_master_interval_ms"], app.cfg.sync.color_master_interval_ms);
        	Json::getBoolTolerant(jsync["color_slave_enabled"], app.cfg.sync.color_slave_enabled);
        	Json::getValue(jsync["color_slave_topic"], app.cfg.sync.color_slave_topic);
        }

        JsonObject jevents = root["events"];
        if (!jevents.isNull()) {
        	Json::getValue(jevents["color_interval_ms"], app.cfg.events.color_interval_ms);
        	Json::getValue(jevents["color_mininterval_ms"], app.cfg.events.color_mininterval_ms);
        	Json::getBoolTolerant(jevents["server_enabled"], app.cfg.events.server_enabled);
        	Json::getValue(jevents["transfin_interval_ms"], app.cfg.events.transfin_interval_ms);
        }

        app.cfg.sanitizeValues();

        // update and save settings if we haven`t received any error until now
        if (!error) {
        	bool restart = root["restart"] | false;
            if (ip_updated) {
            	if (restart) {
					debug_i("ApplicationWebserver::onConfig ip settings changed - rebooting");
					app.delayedCMD("restart", 3000); // wait 3s to first send response
					//json["data"] = "restart";
                }
            }
            if (ap_updated) {
				if (restart && WifiAccessPoint.isEnabled()) {
					debug_i("ApplicationWebserver::onConfig wifiap settings changed - rebooting");
					app.delayedCMD("restart", 3000); // wait 3s to first send response
					//json["data"] = "restart";

				}
            }
            if (color_updated) {
                debug_d("ApplicationWebserver::onConfig color settings changed - refreshing");

                //refresh settings
                app.rgbwwctrl.setup();

                //refresh current output
                app.rgbwwctrl.refresh();

            }
            app.cfg.save();
            sendApiCode(response, API_CODES::API_SUCCESS);
        } else {
            sendApiCode(response, API_CODES::API_MISSING_PARAM, error_msg);
        }

    } else {
        JsonObjectStream* stream = new JsonObjectStream(CONFIG_MAX_LENGTH);
        JsonObject json = stream->getRoot();
        // returning settings
        JsonObject net = json.createNestedObject("network");
        JsonObject con = net.createNestedObject("connection");
        con["dhcp"] = WifiStation.isEnabledDHCP();

        //con["ip"] = WifiStation.getIP().toString();
        //con["netmask"] = WifiStation.getNetworkMask().toString();
        //con["gateway"] = WifiStation.getNetworkGateway().toString();

        con["ip"] = app.cfg.network.connection.ip.toString();
        con["netmask"] = app.cfg.network.connection.netmask.toString();
        con["gateway"] = app.cfg.network.connection.gateway.toString();

        JsonObject ap = net.createNestedObject("ap");
        ap["secured"] = app.cfg.network.ap.secured;
        ap["password"] = app.cfg.network.ap.password;
        ap["ssid"] = app.cfg.network.ap.ssid;

        JsonObject mqtt = net.createNestedObject("mqtt");
        mqtt["enabled"] = app.cfg.network.mqtt.enabled;
        mqtt["server"] = app.cfg.network.mqtt.server;
        mqtt["port"] = app.cfg.network.mqtt.port;
        mqtt["username"] = app.cfg.network.mqtt.username;
        mqtt["password"] = app.cfg.network.mqtt.password;
        mqtt["topic_base"] = app.cfg.network.mqtt.topic_base;

        JsonObject color = json.createNestedObject("color");
        color["outputmode"] = app.cfg.color.outputmode;
        color["startup_color"] = app.cfg.color.startup_color;

        JsonObject hsv = color.createNestedObject("hsv");
        hsv["model"] = app.cfg.color.hsv.model;

        hsv["red"] = app.cfg.color.hsv.red;
        hsv["yellow"] = app.cfg.color.hsv.yellow;
        hsv["green"] = app.cfg.color.hsv.green;
        hsv["cyan"] = app.cfg.color.hsv.cyan;
        hsv["blue"] = app.cfg.color.hsv.blue;
        hsv["magenta"] = app.cfg.color.hsv.magenta;

        JsonObject brighntess = color.createNestedObject("brightness");
        brighntess["red"] = app.cfg.color.brightness.red;
        brighntess["green"] = app.cfg.color.brightness.green;
        brighntess["blue"] = app.cfg.color.brightness.blue;
        brighntess["ww"] = app.cfg.color.brightness.ww;
        brighntess["cw"] = app.cfg.color.brightness.cw;

        JsonObject ctmp = color.createNestedObject("colortemp");
        ctmp["ww"] = app.cfg.color.colortemp.ww;
        ctmp["cw"] = app.cfg.color.colortemp.cw;

        JsonObject s = json.createNestedObject("security");
        s["api_secured"] = app.cfg.general.api_secured;

        JsonObject ota = json.createNestedObject("ota");
        ota["url"] = app.cfg.general.otaurl;

        JsonObject sync = json.createNestedObject("sync");
        sync["clock_master_enabled"] = app.cfg.sync.clock_master_enabled;
        sync["clock_master_interval"] = app.cfg.sync.clock_master_interval;
        sync["clock_slave_enabled"] = app.cfg.sync.clock_slave_enabled;
        sync["clock_slave_topic"] = app.cfg.sync.clock_slave_topic;
        sync["cmd_master_enabled"] = app.cfg.sync.cmd_master_enabled;
        sync["cmd_slave_enabled"] = app.cfg.sync.cmd_slave_enabled;
        sync["cmd_slave_topic"] = app.cfg.sync.cmd_slave_topic;

        sync["color_master_enabled"] = app.cfg.sync.color_master_enabled;
        sync["color_master_interval_ms"] = app.cfg.sync.color_master_interval_ms;
        sync["color_slave_enabled"] = app.cfg.sync.color_slave_enabled;
        sync["color_slave_topic"] = app.cfg.sync.color_slave_topic;

        JsonObject events = json.createNestedObject("events");
        events["color_interval_ms"] = app.cfg.events.color_interval_ms;
        events["color_mininterval_ms"] = app.cfg.events.color_mininterval_ms;
        events["server_enabled"] = app.cfg.events.server_enabled;
        events["transfin_interval_ms"] = app.cfg.events.transfin_interval_ms;

        JsonObject general = json.createNestedObject("general");
        general["device_name"] = app.cfg.general.device_name;
        general["pin_config"] = app.cfg.general.pin_config;
        general["buttons_config"] = app.cfg.general.buttons_config;
        general["buttons_debounce_ms"] = app.cfg.general.buttons_debounce_ms;

        sendApiResponse(response, stream);
    }
}

void ApplicationWebserver::onInfo(HttpRequest &request, HttpResponse &response) {
    if (!checkHeap(response))
        return;

    if (!authenticated(request, response)) {
        return;
    }

#ifdef ARCH_ESP8266
    if (app.ota.isProccessing()) {
        sendApiCode(response, API_CODES::API_UPDATE_IN_PROGRESS);
        return;
    }
#endif

    if (request.method != HTTP_GET) {
        sendApiCode(response, API_CODES::API_BAD_REQUEST, "not GET");
        return;
    }

    JsonObjectStream* stream = new JsonObjectStream();
    JsonObject data = stream->getRoot();
    data["deviceid"] = String(system_get_chip_id());
    data["current_rom"] = String(app.ota.getRomPartition().name());
    data["git_version"] = fw_git_version;
    data["git_date"] = fw_git_date;
    data["webapp_version"] = WEBAPP_VERSION;
    data["sming"] = SMING_VERSION;
    data["event_num_clients"] = app.eventserver.activeClients;
    data["uptime"] = app.getUptime();
    data["heap_free"] = system_get_free_heap_size();
    #ifdef ARCH_ESP8266
        data["soc"]=F("Esp8266");
    #elif ARCH_ESP32
        data["soc"]=F("Esp32");
    #endif   
    #ifdef PART_LAYOUT
        data["part_layout"]=PART_LAYOUT;
    #else
        data["part_layout"]=F("v1");
    #endif

/*
    FileSystem::Info fsInfo;
    app.getinfo(fsInfo);
    JsonObject FS=data.createNestedObject("fs");
    FS["mounted"] = fsInfo.partition?"true":"false";
    FS["size"] = fsInfo.total;
    FS["used"] = fsInfo.used;
    FS["available"] = fsInfo.freeSpace;
*/
    JsonObject rgbww = data.createNestedObject("rgbww");
    rgbww["version"] = RGBWW_VERSION;
    rgbww["queuesize"] = RGBWW_ANIMATIONQSIZE;

    JsonObject con = data.createNestedObject("connection");
    con["connected"] = WifiStation.isConnected();
    con["ssid"] = WifiStation.getSSID();
    con["dhcp"] = WifiStation.isEnabledDHCP();
    con["ip"] = WifiStation.getIP().toString();
    con["netmask"] = WifiStation.getNetworkMask().toString();
    con["gateway"] = WifiStation.getNetworkGateway().toString();
    con["mac"] = WifiStation.getMAC();
    //con["mdnshostname"] = app.cfg.network.connection.mdnshostname.c_str();

    sendApiResponse(response, stream);
}


void ApplicationWebserver::onColorGet(HttpRequest &request, HttpResponse &response) {
    if (!checkHeap(response))
        return;

    JsonObjectStream* stream = new JsonObjectStream();
    JsonObject json = stream->getRoot();

    JsonObject raw = json.createNestedObject("raw");
    ChannelOutput output = app.rgbwwctrl.getCurrentOutput();
    raw["r"] = output.r;
    raw["g"] = output.g;
    raw["b"] = output.b;
    raw["ww"] = output.ww;
    raw["cw"] = output.cw;

    JsonObject hsv = json.createNestedObject("hsv");
    float h, s, v;
    int ct;
    HSVCT c = app.rgbwwctrl.getCurrentColor();
    c.asRadian(h, s, v, ct);
    hsv["h"] = h;
    hsv["s"] = s;
    hsv["v"] = v;
    hsv["ct"] = ct;

    sendApiResponse(response, stream);
}

void ApplicationWebserver::onColorPost(HttpRequest &request, HttpResponse &response) {
    String body = request.getBody();
    if (body == NULL) {
        sendApiCode(response, API_CODES::API_BAD_REQUEST, "no body");
        return;

    }

    String msg;
    debug_i("received color update wirh message %s", msg.c_str());
    if (!app.jsonproc.onColor(body, msg)) {
        sendApiCode(response, API_CODES::API_BAD_REQUEST, msg);
    }
    else {

        sendApiCode(response, API_CODES::API_SUCCESS);
    }
}

void ApplicationWebserver::onColor(HttpRequest &request, HttpResponse &response) {
    if (!authenticated(request, response)) {
        return;
    }

#ifdef ARCH_ESP8266
    if (app.ota.isProccessing()) {
        sendApiCode(response, API_CODES::API_UPDATE_IN_PROGRESS);
        return;
    }
#endif

    if (request.method != HTTP_POST && request.method != HTTP_GET && request.method!=HTTP_OPTIONS) {
        sendApiCode(response, API_CODES::API_BAD_REQUEST, "not POST, GET or OPTIONS");
        return;
    }

    if (request.method==HTTP_OPTIONS){
        sendApiCode(response, API_CODES::API_SUCCESS);
        return;
    }

    bool error = false;
    if (request.method == HTTP_POST) {
        ApplicationWebserver::onColorPost(request, response);
    } else {
        ApplicationWebserver::onColorGet(request, response);
    }

}

bool ApplicationWebserver::isPrintable(String& str) {
    for (unsigned int i=0; i < str.length(); ++i)
    {
        char c = str[i];
        if (c < 0x20)
            return false;
    }
    return true;
}

void ApplicationWebserver::onNetworks(HttpRequest &request, HttpResponse &response) {

    if (!authenticated(request, response)) {
        return;
    }

#ifdef ARCH_ESP8266
    if (app.ota.isProccessing()) {
        sendApiCode(response, API_CODES::API_UPDATE_IN_PROGRESS);
        return;
    }
#endif
    if (request.method == HTTP_OPTIONS){
        response.setHeader("Access-Control-Allow-Origin", "*"); // allow CORS temporarily for testing, probaly best to remove it later as
                                                                // it may be a security risk to allow $world to scan for wifi networks
        sendApiCode(response, API_CODES::API_SUCCESS);
        return;
    }
    if (request.method != HTTP_GET) {
        sendApiCode(response, API_CODES::API_BAD_REQUEST, "not HTTP GET");
        return;
    }

    JsonObjectStream* stream = new JsonObjectStream();
    JsonObject json = stream->getRoot();

    bool error = false;

    if (app.network.isScanning()) {
        json["scanning"] = true;
    } else {
        json["scanning"] = false;
        JsonArray netlist = json.createNestedArray("available");
        BssList networks = app.network.getAvailableNetworks();
        for (unsigned int i = 0; i < networks.count(); i++) {
            if (networks[i].hidden)
                continue;

            // SSIDs may contain any byte values. Some are not printable and will cause the javascript client to fail
            // on parsing the message. Try to filter those here
            if (!ApplicationWebserver::isPrintable(networks[i].ssid)) {
                debug_w("Filtered SSID due to unprintable characters: %s", networks[i].ssid.c_str());
                continue;
            }

            JsonObject item = netlist.createNestedObject();
            item["id"] = (int) networks[i].getHashId();
            item["ssid"] = networks[i].ssid;
            item["signal"] = networks[i].rssi;
            item["encryption"] = networks[i].getAuthorizationMethodName();
            //limit to max 25 networks
            if (i >= 25)
                break;
        }
    }
    response.setHeader("Access-Control-Allow-Origin", "*"); // allow CORS temporarily for testing, probaly best to remove it later as
                                                            // it may be a security risk to allow $world to scan for wifi networks
    sendApiResponse(response, stream);
}

void ApplicationWebserver::onScanNetworks(HttpRequest &request, HttpResponse &response) {

    if (!authenticated(request, response)) {
        return;
    }

#ifdef ARCH_ESP8266
    if (app.ota.isProccessing()) {
        sendApiCode(response, API_CODES::API_UPDATE_IN_PROGRESS);
        return;
    }
#endif

    if (request.method != HTTP_POST) {
        sendApiCode(response, API_CODES::API_BAD_REQUEST, "not HTTP POST");
        return;
    }
    if (!app.network.isScanning()) {
        app.network.scan(false);
    }

    sendApiCode(response, API_CODES::API_SUCCESS);
}

void ApplicationWebserver::onConnect(HttpRequest &request, HttpResponse &response) {

    if (!authenticated(request, response)) {
        return;
    }

#ifdef ARCH_ESP8266
    if (app.ota.isProccessing()) {
        sendApiCode(response, API_CODES::API_UPDATE_IN_PROGRESS);
        return;
    }
#endif

    if (request.method != HTTP_POST && request.method != HTTP_GET) {
        sendApiCode(response, API_CODES::API_BAD_REQUEST, "not HTTP POST or GET");
        return;
    }

    if (request.method == HTTP_POST) {

        String body = request.getBody();
        if (body == NULL) {

            sendApiCode(response, API_CODES::API_BAD_REQUEST, "could not get HTTP body");
            return;

        }
        DynamicJsonDocument doc(1024);
        Json::deserialize(doc, body);
        String ssid;
        String password;
        if (Json::getValue(doc["ssid"], ssid)) {
        	password = doc["password"].as<const char*>();
            debug_d("ssid %s - pass %s", ssid.c_str(), password.c_str());
            app.network.connect(ssid, password, true);
            sendApiCode(response, API_CODES::API_SUCCESS);
            return;

        } else {
            sendApiCode(response, API_CODES::API_MISSING_PARAM);
            return;
        }
    } else {
        JsonObjectStream* stream = new JsonObjectStream();
        JsonObject json = stream->getRoot();

        CONNECTION_STATUS status = app.network.get_con_status();
        json["status"] = int(status);
        if (status == CONNECTION_STATUS::ERROR) {
            json["error"] = app.network.get_con_err_msg();
        } else if (status == CONNECTION_STATUS::CONNECTED) {
            // return connected
            if (app.cfg.network.connection.dhcp) {
                json["ip"] = WifiStation.getIP().toString();
            } else {
                json["ip"] = app.cfg.network.connection.ip.toString();
            }
            json["dhcp"] = app.cfg.network.connection.dhcp;
            json["ssid"] = WifiStation.getSSID();

        }
        sendApiResponse(response, stream);
    }
}



void ApplicationWebserver::onSystemReq(HttpRequest &request, HttpResponse &response) {

    if (!authenticated(request, response)) {
        return;
    }

#ifdef ARCH_ESP8266
    if (app.ota.isProccessing()) {
        sendApiCode(response, API_CODES::API_UPDATE_IN_PROGRESS);
        return;
    }
#endif

    if(request.method == HTTP_OPTIONS){
        response.setHeader("Access-Control-Allow-Origin", "*"); // allow CORS temporarily for testing, probaly best to remove it later as
                                                                // it may be a security risk to allow $world to scan for wifi networks
        sendApiCode(response, API_CODES::API_SUCCESS);
        return;
    }
    if (request.method != HTTP_POST) {
        sendApiCode(response, API_CODES::API_BAD_REQUEST, "not HTTP POST");
        return;
    }

    bool error = false;
    String body = request.getBody();
    if (body == NULL) {
        sendApiCode(response, API_CODES::API_BAD_REQUEST, "could not get HTTP body");
        return;
    } else {
        debug_i("ApplicationWebserver::onSystemReq: %s", body.c_str());
        DynamicJsonDocument doc(1024);
        Json::deserialize(doc, body);

        String cmd = doc["cmd"].as<const char*>();
        if (cmd) {
            if (cmd.equals("debug")) {
            	bool enable;
            	if (Json::getValue(doc["enable"], enable)) {
                    Serial.systemDebugOutput(enable);
                } else {
                    error = true;
                }

            }else if (!app.delayedCMD(cmd, 1500)) {
                error = true;
            }

        } else {
            error = true;
        }

    }
        response.setHeader("Access-Control-Allow-Origin", "*"); // allow CORS temporarily for testing, probaly best to remove it later as
                                                                // it may be a security risk to allow $world to scan for wifi networks
    
    if (!error) {
        sendApiCode(response, API_CODES::API_SUCCESS);
    } else {
        sendApiCode(response, API_CODES::API_MISSING_PARAM);
    }

}

void ApplicationWebserver::onUpdate(HttpRequest &request, HttpResponse &response) {
    if (!authenticated(request, response)) {
        return;
    }

#ifdef ARCH_HOST
    sendApiCode(response, API_CODES::API_BAD_REQUEST, "not supported on Host");
    return;
#else
    if (request.method == HTTP_OPTIONS){
        // probably a CORS request
        response.setHeader("Access-Control-Allow-Origin", "*");
        sendApiCode(response,API_CODES::API_SUCCESS,"");
        debug_i("/update HTTP_OPTIONS Request, sent API_SUCCSSS");
        return;
    }   if (request.method != HTTP_POST && request.method != HTTP_GET) {
        sendApiCode(response, API_CODES::API_BAD_REQUEST, "not HTTP POST or GET");
        return;
    }

    if (request.method == HTTP_POST) {
        if (app.ota.isProccessing()) {
            sendApiCode(response, API_CODES::API_UPDATE_IN_PROGRESS);
            return;
        }

        String body = request.getBody();
        if (body == NULL) {
            sendApiCode(response, API_CODES::API_BAD_REQUEST, "could not parse HTTP body");
            return;
        }

        debug_i("body: %s", body.c_str());
        DynamicJsonDocument doc(1024);
            Json::deserialize(doc, body);

        String romurl;
        Json::getValue(doc["rom"]["url"],romurl);
        
        String spiffsurl;
        Json::getValue(doc["spiffs"]["url"],spiffsurl);
        
        debug_i("starting update process with \n    webapp: %s\n    spiffs: %s", romurl.c_str(), spiffsurl.c_str());
        if (! romurl || ! spiffsurl) {
            sendApiCode(response, API_CODES::API_MISSING_PARAM);
        } else {
            app.ota.start(romurl, spiffsurl);
            response.setHeader("Access-Control-Allow-Origin", "*");
            sendApiCode(response, API_CODES::API_SUCCESS);
        }
        return;
    }
    JsonObjectStream* stream = new JsonObjectStream();
    JsonObject json = stream->getRoot();
    json["status"] = int(app.ota.getStatus());
    sendApiResponse(response, stream);
#endif
}

//simple call-response to check if we can reach server
void ApplicationWebserver::onPing(HttpRequest &request, HttpResponse &response) {
    if (request.method != HTTP_GET) {
        sendApiCode(response, API_CODES::API_BAD_REQUEST, "not HTTP GET");
        return;
    }
    JsonObjectStream* stream = new JsonObjectStream();
    JsonObject json = stream->getRoot();
    json["ping"] = "pong";
    sendApiResponse(response, stream);
}

void ApplicationWebserver::onStop(HttpRequest &request, HttpResponse &response) {
    if (request.method != HTTP_POST) {
        sendApiCode(response, API_CODES::API_BAD_REQUEST, "not HTTP POST");
        return;
    }

    String msg;
    if (app.jsonproc.onStop(request.getBody(), msg, true)) {
        sendApiCode(response, API_CODES::API_SUCCESS);
    }
    else {
        sendApiCode(response, API_CODES::API_BAD_REQUEST);
    }
}

void ApplicationWebserver::onSkip(HttpRequest &request, HttpResponse &response) {
    if (request.method != HTTP_POST) {
        sendApiCode(response, API_CODES::API_BAD_REQUEST, "not HTTP POST");
        return;
    }

    String msg;
    if (app.jsonproc.onSkip(request.getBody(), msg)) {
        sendApiCode(response, API_CODES::API_SUCCESS);
    }
    else {
        sendApiCode(response, API_CODES::API_BAD_REQUEST);
    }
}

void ApplicationWebserver::onPause(HttpRequest &request, HttpResponse &response) {
    if (request.method != HTTP_POST) {
        sendApiCode(response, API_CODES::API_BAD_REQUEST, "not HTTP POST");
        return;
    }

    String msg;
    if (app.jsonproc.onPause(request.getBody(), msg, true)) {
        sendApiCode(response, API_CODES::API_SUCCESS);
    }
    else {
        sendApiCode(response, API_CODES::API_BAD_REQUEST);
    }
}

void ApplicationWebserver::onContinue(HttpRequest &request, HttpResponse &response) {
    if (request.method != HTTP_POST) {
        sendApiCode(response, API_CODES::API_BAD_REQUEST, "not HTTP POST");
        return;
    }

    String msg;
    if (app.jsonproc.onContinue(request.getBody(), msg)) {
        sendApiCode(response, API_CODES::API_SUCCESS);
    }
    else {
        sendApiCode(response, API_CODES::API_BAD_REQUEST);
    }
}

void ApplicationWebserver::onBlink(HttpRequest &request, HttpResponse &response) {
    if (request.method != HTTP_POST) {
        sendApiCode(response, API_CODES::API_BAD_REQUEST, "not HTTP POST");
        return;
    }

    String msg;
    if (app.jsonproc.onBlink(request.getBody(), msg)) {
        sendApiCode(response, API_CODES::API_SUCCESS);
    }
    else {
        sendApiCode(response, API_CODES::API_BAD_REQUEST);
    }
}

void ApplicationWebserver::onToggle(HttpRequest &request, HttpResponse &response) {
    if (request.method != HTTP_POST) {
        sendApiCode(response, API_CODES::API_BAD_REQUEST, "not HTTP POST");
        return;
    }

    String msg;
    if (app.jsonproc.onToggle(request.getBody(), msg)) {
        sendApiCode(response, API_CODES::API_SUCCESS);
    }
    else {
        sendApiCode(response, API_CODES::API_BAD_REQUEST);
    }
}

void ApplicationWebserver::onStorage(HttpRequest &request, HttpResponse &response){
    if (request.method != HTTP_POST && request.method != HTTP_GET && request.method!=HTTP_OPTIONS) {
        sendApiCode(response, API_CODES::API_BAD_REQUEST, "not POST, GET or OPTIONS request");
        return;
    }
    
    /*
    / axios sends a HTTP_OPTIONS request to check if server is CORS permissive (which this firmware 
    / has been for years) this is just to reply to that request in order to pass the CORS test
    */
    if (request.method == HTTP_OPTIONS){
        // probably a CORS request
        sendApiCode(response,API_CODES::API_SUCCESS,"");
        debug_i("HTTP_OPTIONS Request, sent API_SUCCSSS");
        return;
    }
    
    if (request.method == HTTP_POST) {
        debug_i("======================\nHTTP POST request received, ");
        String header=request.getHeader("Content-type");
        if(header!="application/json"){
            sendApiCode(response,API_BAD_REQUEST,"only json content allowed");
        }
        debug_i("got post with content type %s", header.c_str());
        String body = request.getBody();
        if (body == NULL || body.length()>FILE_MAX_SIZE) {

            sendApiCode(response, API_CODES::API_BAD_REQUEST, "could not parse HTTP body");
            return;
        }

        bool error = false;
        
        debug_i("body length: %i", body.length());
        DynamicJsonDocument doc(body.length()+32);
        Json::deserialize(doc, body);
        String fileName=doc["filename"];
        
        //DynamicJsonDocument data(body.length()+32);
        //Json::deserialize(data, Json::serialize(doc["data"]));
        //doc.clear(); //clearing the original document to save RAM
        debug_i("will save to file %s", fileName.c_str());
        debug_i("original document uses %i bytes", doc.memoryUsage());
        String data=doc["data"];
        debug_i("data: %s", data.c_str());
        
        FileHandle file=fileOpen(fileName.c_str(),IFS::OpenFlag::Write|IFS::OpenFlag::Create|IFS::OpenFlag::Truncate);
        if(!fileWrite(file, data.c_str(), data.length())){
            debug_e("Saving config to file %s failed!", fileName.c_str());
        }
        fileClose(file);
        response.setAllowCrossDomainOrigin("*");
        sendApiCode(response, API_CODES::API_SUCCESS);
        return;       
    }
}

void ApplicationWebserver::onHosts(HttpRequest &request, HttpResponse &response){
    if (request.method != HTTP_GET && request.method!=HTTP_OPTIONS) {
        sendApiCode(response, API_CODES::API_BAD_REQUEST, "not GET or OPTIONS request");
        return;
    }
    
    if (request.method == HTTP_OPTIONS){
        // probably a CORS request
        sendApiCode(response,API_CODES::API_SUCCESS,"");
        debug_i("HTTP_OPTIONS Request, sent API_SUCCSSS");
        return;
    }
    
    String myHosts;
    // Set the response body with the JSON
    response.setHeader("Access-Control-Allow-Origin", "*");
    response.setContentType("application/json");
    response.sendString(app.network.getMdnsHosts());

    return;
}

 void ApplicationWebserver::onObject(HttpRequest &request, HttpResponse &response){
    if (request.method == HTTP_OPTIONS){
        // probably a CORS request
        response.setHeader("Access-Control-Allow-Origin", "*");
        sendApiCode(response,API_CODES::API_SUCCESS,"");
        debug_i("HTTP_OPTIONS Request, sent API_SUCCSSS");
        return;
    }
    /******************************************************************************************************
    *  valid object types are:
    *
    * g: group
    * {id: <id>, name: <string>, hosts:[hostid, hostid, hostid, hostid, ...]}
    * 
    * p: preset
    * {id: <id>, name: <string>, hsv:{h: <float>, s: <float>, v: <float>}}
    * 
    * p: preset
    * {id: <id>, name: <string>, raw:{r: <float>, g: <float>, b: <float>, ww: <float>, cw: <float>}}
    * 
    * h: host
    * {id: <id>, name: <string>, ip: <string>, active: <bool>}
    * remarkt: the active field shall be added upon sending the file by checking, if the host is in the current mDNS hosts list
    * 
    * s: scene
    * {id: <id>, name: <string>, hosts: [{id: <hostid>,hsv:{h: <float>, s: <float>, v: <float>},...]}
    * 
    * enumerating all objects of a type is done by first sending a GET request to /object?type=<type> which the 
    * controller will reply to with a json array of all objects of the requested type in the following format:
    * {"<type>":["2234585-1233362","2234585-0408750","2234585-9433038","2234585-7332130","2234585-7389644"]}
    * it is then the job of the front end to request each object individually by sending a GET request to 
    * /object?type=<type>&id=<id>
    * 
    * creating a new object is done by sending a POST request to /object?type=<type> with the json object as 
    * described above as the body.
    * The id field (both in the url as well as in the json object) should be omitted, in which case the 
    * controller will generate a new id for the object.
    * 
    * updating an existing object is done by sending a POST request to /object?type=<type>&id=<id> with the fully populated
    * json object as the body. In this case the id field in the json object must match the id in the url.
    * 
    * deleting an object is done by sending a DELETE request to /object?type=<type>&id=<id>. No checks are performed.
    * 
    * it's important to understand that the controller only stores the objects, the frontend is fully responsible
    * for the cohesion of the data. If a non-existant host is added to a scene, the controller will not complain.
    * 
    * Since the id for the hosts is the actual ESP8266 it is possible to track controllers through ip address changes
    * and keep their ids constant. This is not implemented yet.
    ******************************************************************************************************/
    String objectType = request.getQueryParameter("type");
    String objectId = request.getQueryParameter("id");
    #ifdef DEBUG_OBJECT_API
    debug_i("got request with uri %s for object type %s with id %s.",String(request.uri).c_str(), objectType.c_str(), objectId.c_str());
    #endif
    auto tcpConnections=getConnections();
    debug_i("===> (objEntry) nr of tcpConnections: %i", tcpConnections.size());

    if (objectType==""){
        #ifdef DEBUG_OBJECT_API
        debug_i("missing object type");        
        #endif
        response.setHeader("Access-Control-Allow-Origin", "*");
        sendApiCode(response, API_CODES::API_BAD_REQUEST, "missing object type");
        return;
    }
    String types=F("gphs");
    if (types.indexOf(objectType)==-1||objectType.length()>1){
        #ifdef DEBUG_OBJECT_API
        debug_i("unsupported object type");
        #endif
        response.setHeader("Access-Control-Allow-Origin", "*");
        sendApiCode(response, API_CODES::API_BAD_REQUEST, "unsupported object type");
        return;
    }
    
    if( request.method == HTTP_GET) {
        if (objectId==""){
        
            //requested object type but no object id, list all objects of type
            Directory dir;
            if(!dir.open()) {
                debug_i("could not open dir");
                sendApiCode(response, API_CODES::API_BAD_REQUEST, "could not open dir");
                return;
            }else{
                JsonObjectStream* stream = new JsonObjectStream(CONFIG_MAX_LENGTH);
                JsonObject doc = stream->getRoot();

                JsonArray objectsList;

                switch (objectType.c_str()[0]) {
                    case 'g':
                        objectsList = doc.createNestedArray("groups"); // Assign value inside the case
                        break;
                        ;;
                    case 'p':
                        objectsList = doc.createNestedArray("presets"); // Assign value inside the case
                        break;
                        ;;
                    case 'h':
                        objectsList = doc.createNestedArray("hosts"); // Assign value inside the case
                        break;
                        ;;
                    case 's':
                        objectsList = doc.createNestedArray("scenes"); // Assign value inside the case
                        break;
                        ;;
                }

                while(dir.next()) {
                    String fileName=String(dir.stat().name);
                    #ifdef DEBUG_OBJECT_API
                    debug_i("found file: %s",fileName.c_str());
                    debug_i("file begins with %s",fileName.substring(1,2).c_str()); 
                    #endif
                    if(fileName.substring(1,2)==objectType){
                        #ifdef DEBUG_OBJECT_API
                        debug_i("adding file %s to list",fileName);
                        debug_i("filename %s, extension starts at %i",fileName,fileName.indexOf(F(".")));
                        #endif
                        objectId=fileName.substring(2, fileName.indexOf(F(".")));
                        objectsList.add(objectId);
                    }
                }
                response.setContentType("application/json");
                response.setAllowCrossDomainOrigin("*");
                response.setHeader("Access-Control-Allow-Origin", "*");
                response.sendString(Json::serialize(doc));
                delete stream;
            }
        }else{
            //got GET with object type and id, return object, if available
            debug_i("HTTP GET request received, ");
            String fileName = "_"+objectType+ objectId + ".json"; 
            if (!fileName) {
                #ifdef DEBUG_OBJECT_API
                debug_i("file not found");
                #endif
                response.setHeader("Access-Control-Allow-Origin", "*");
                sendApiCode(response, API_CODES::API_BAD_REQUEST, "file not found");
                return;
            }
            response.setContentType("application/json");
            response.setAllowCrossDomainOrigin("*");
            #ifdef DEBUG_OBJECT_API
            debug_i("sending file %s", fileName.c_str());
            #endif
            response.setHeader("Access-Control-Allow-Origin", "*");
            response.sendFile(fileName);
            return;
        }
    }
    if (request.method==HTTP_POST){
        debug_i(   "HTTP PUT request received, ");
        String body = request.getBody();
        #ifdef DEBUG_OBJECT_API
        debug_i("request body: %s", body.c_str());
        #endif
        if (body == NULL || body.length()>FILE_MAX_SIZE) {
            response.setHeader("Access-Control-Allow-Origin", "*");
            sendApiCode(response, API_CODES::API_BAD_REQUEST, "could not parse HTTP body");
            #ifdef DEBUG_OBJECT_API
            debug_i("body is null or too long");
            #endif
            return;
        }
        StaticJsonDocument<FILE_MAX_SIZE> doc;
        DeserializationError error = deserializeJson(doc, body);
        if (error) {
            response.setHeader("Access-Control-Allow-Origin", "*");
            sendApiCode(response, API_CODES::API_BAD_REQUEST, "could not parse json from HTTP body");
            #ifdef DEBUG_OBJECT_API
            debug_i("could not parse json");
            #endif
            return;
        }
        #ifdef DEBUG_OBJECT_API
        debug_i("parsed json, found name %s",String(doc["name"]).c_str());
        #endif
        if(objectId==""){
            //no object id, create new object
            if(doc["id"]!=""){
                objectId=String(doc["id"]);
            }else{
               debug_i("no object id, creating new object");
               objectId=makeId();
               doc["id"]=objectId;
            }
        }
        String fileName = "_"+objectType+objectId + ".json"; 
        #ifdef DEBUG_OBJECT_API
        debug_i("will save to file %s", fileName.c_str());
        #endif
        FileHandle file = fileOpen(fileName.c_str(), IFS::OpenFlag::Write|IFS::OpenFlag::Create|IFS::OpenFlag::Truncate);
        if (!file) {
            sendApiCode(response, API_CODES::API_BAD_REQUEST, "file not found");
            #ifdef DEBUG_OBJECT_API
            debug_i("couldn not open file for write");
            #endif
            return;
        }
        String bodyData;
        serializeJson(doc, bodyData);
        #ifdef DEBUG_OBJECT_API
        debug_i("body length: %i", bodyData.length());
        debug_i("data: %s", bodyData.c_str());
        #endif
        if(!fileWrite(file, bodyData.c_str(), bodyData.length())){
            #ifdef DEBUG_OBJECT_API
            debug_e("Saving config to file %s failed!", fileName.c_str());
            //should probably also send some error message to the client
            #endif
        }
        fileClose(file);

        response.setAllowCrossDomainOrigin("*");
        response.setContentType("application/json");
        //doc.clear();
        doc["id"]=objectId;
        bodyData="";
        serializeJson(doc, bodyData);
        response.sendString(bodyData.c_str());

        // send websocket message to all connected clients to 
        // update them about the new object
        JsonRpcMessage msg("preset");
        JsonObject root = msg.getParams();
        root.set(doc.as<JsonObject>());        
        debug_i("rpc: root =%s",Json::serialize(root).c_str());
        debug_i("rpc: msg =%s",Json::serialize(msg.getRoot()).c_str());
        
        String jsonStr = Json::serialize(msg.getRoot());

        wsBroadcast(jsonStr);
        //sendApiCode(response, API_CODES::API_SUCCESS);

        return;       
    }
    if (request.method==HTTP_DELETE){
        String fileName = "_"+objectType+objectId + ".json"; 
        FileHandle file = fileDelete(fileName.c_str());
        if (!file) {
            sendApiCode(response, API_CODES::API_BAD_REQUEST, "file not found");
            return;
        }
        fileClose(file);
        response.setAllowCrossDomainOrigin("*");
        sendApiCode(response, API_CODES::API_SUCCESS);
        return;       
    }

}

String ApplicationWebserver::makeId(){
    /*
     * generate ID for an object. The id is comprised of a letter, denoting the 
     * class of the current object (preset, group, host or scene) the 7 digit  
     * controller id, a dash and the seven lowest digits of the current microsecond 
     * timestamp. There is a very small chance of collision, and in this case, an  
     * existing preset with the colliding id will just be overwritten as if it had
     * been updated. But as said, I recon the chance that a 2nd id will be generatd
     * on the same controller with the exact same microsecond timestamp is very small
     * names, on the other hand, are not relevant for the system, so they can be pickd
     * freely and technically, objects can even be renamed.
     */
    char ___id[8];
    sprintf(___id, "%07u",(uint32_t)micros()%10000000);
    String __id=String(___id);
    String chipId=String(system_get_chip_id());
    String objectId=chipId+"-"+__id;
    #ifdef DEBUG_OBJECT_API
    debug_i("generated id %s ",objectId.c_str());
    #endif
    return objectId;
}
