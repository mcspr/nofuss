/*

NOFUSS Client
Copyright (C) 2016-2017 by Xose Pérez <xose dot perez at gmail dot com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "NoFUSSClient.h"

#include <ArduinoJson.h>
#include <ESP8266httpUpdate.h>

#include <functional>

const char NoFUSSUserAgent[] PROGMEM = "NoFussClient";
constexpr unsigned long NoFUSSTimeout { 1000ul };

namespace {
String normalizeUrl(const String& server, const String& path) {
    if (path.startsWith("http")) {
        return path;
    }

    return server + '/' + path;
}

} // namespace

void NoFUSSClientClass::setServer(String server) {
    _server = server;
}

void NoFUSSClientClass::setDevice(String device) {
    _device = device;
}

void NoFUSSClientClass::setVersion(String version) {
    _version = version;
}

void NoFUSSClientClass::setBuild(String build) {
    _build = build;
}

void NoFUSSClientClass::onMessage(TMessageFunction fn) {
    _callback = fn;
}

String NoFUSSClientClass::getNewVersion() {
    return _newVersion;
}

String NoFUSSClientClass::getNewFirmware() {
    return _newFirmware;
}

String NoFUSSClientClass::getNewFileSystem() {
    return _newFileSystem;
}

int NoFUSSClientClass::getErrorNumber() {
    return _errorNumber;
}

String NoFUSSClientClass::getErrorString() {
    return _errorString;
}

void NoFUSSClientClass::_doCallback(nofuss_t message) {
    if (_callback != NULL) _callback(message);
}

String NoFUSSClientClass::_getPayload() {

    String payload = "";

    WiFiClient client;
    HTTPClient http;
    http.begin(client, _server.c_str());

    http.useHTTP10(true);
    http.setReuse(false);
    http.setTimeout(NoFUSSTimeout);
    http.setUserAgent(NoFUSSUserAgent);
    http.addHeader(F("X-ESP8266-MAC"), WiFi.macAddress());
    http.addHeader(F("X-ESP8266-DEVICE"), _device);
    http.addHeader(F("X-ESP8266-VERSION"), _version);
    http.addHeader(F("X-ESP8266-BUILD"), _build);
    http.addHeader(F("X-ESP8266-CHIPID"), String(ESP.getChipId()));
    http.addHeader(F("X-ESP8266-CHIPSIZE"), String(ESP.getFlashChipRealSize()));

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        payload = http.getString();
    } else {
        _errorNumber = httpCode;
        _errorString = http.errorToString(httpCode);
    }
    http.end();

    return payload;

}

bool NoFUSSClientClass::_checkUpdates() {

    String payload = _getPayload();
    if (payload.length() == 0) {
        _doCallback(NOFUSS_NO_RESPONSE_ERROR);
        return false;
    }

    StaticJsonBuffer<500> jsonBuffer;
    JsonObject& response = jsonBuffer.parseObject(payload);

    if (!response.success()) {
        _doCallback(NOFUSS_PARSE_ERROR);
        return false;
    }

    if (response.size() == 0) {
        _doCallback(NOFUSS_UPTODATE);
        return false;
    }

    _newVersion = response.get<String>("version");
    _newFirmware = response.get<String>("firmware");

    if (response.containsKey("fs")) {
        _newFileSystem = response.get<String>("fs");
    } else if (response.containsKey("spiffs")) {
        _newFileSystem = response.get<String>("spiffs");
    }

    _doCallback(NOFUSS_UPDATING);
    return true;

}

bool NoFUSSClientClass::_doUpdateCallbacks(t_httpUpdate_return result, nofuss_t success, nofuss_t error) {
    if (result == HTTP_UPDATE_OK) {
        _doCallback(success);
        return true;
    }

    _errorNumber = ESPhttpUpdate.getLastError();
    _errorString = ESPhttpUpdate.getLastErrorString();
    _doCallback(error);

    return false;
}

void NoFUSSClientClass::_doUpdate() {

    bool error = false;
    int updates = 0;

    ESPhttpUpdate.rebootOnUpdate(false);

    if (_newFileSystem.length() > 0) {
        auto url = normalizeUrl(_server, _newFileSystem);

        WiFiClient client;
        t_httpUpdate_return ret = ESPhttpUpdate.updateFS(client, url);
        if (_doUpdateCallbacks(ret, NOFUSS_FILESYSTEM_UPDATED, NOFUSS_FILESYSTEM_UPDATE_ERROR)) {
            ++updates;
        } else {
            error = true;
        }
    }

    if (!error && (_newFirmware.length() > 0)) {
        auto url = normalizeUrl(_server, _newFirmware);

        WiFiClient client;
        t_httpUpdate_return ret = ESPhttpUpdate.update(client, url);

        if (_doUpdateCallbacks(ret, NOFUSS_FIRMWARE_UPDATED, NOFUSS_FIRMWARE_UPDATE_ERROR)) {
            ++updates;
        } else {
            error = true;
        }
    }

    if (!error && (updates > 0)) {
        _doCallback(NOFUSS_RESET);
        ESP.restart();
    }

}

void NoFUSSClientClass::handle() {
    _doCallback(NOFUSS_START);
    if (_checkUpdates()) _doUpdate();
    _doCallback(NOFUSS_END);
}

NoFUSSClientClass NoFUSSClient;
