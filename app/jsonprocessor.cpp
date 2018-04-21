#include <RGBWWCtrl.h>


bool JsonProcessor::onColor(const String& json, String& msg, bool relay) {
    debug_e("JsonProcessor::onColor: %s", json.c_str());
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(json);
    return onColor(root, msg, relay);
}

bool JsonProcessor::onColor(JsonObject& root, String& msg, bool relay) {
    bool result = false;
    if (root["cmds"].success()) {
        Vector<String> errors;
        // multi command post (needs testing)
        const JsonArray& cmds = root["cmds"].asArray();
        for(int i=0; i < cmds.size(); ++i) {
            String msg;
            if (!onSingleColorCommand(cmds[i], msg))
                errors.add(msg);
        }

        if (errors.size() == 0)
            result = true;
        else {
            String msg;
            for (int i=0; i < errors.size(); ++i)
                msg += errors[i] + "|";
            result = false;
        }
    }
    else {
        if (onSingleColorCommand(root, msg))
            result = true;
        else
            result = false;
    }

    if (relay)
        app.onCommandRelay("color", root);

    return result;
}

bool JsonProcessor::onStop(const String& json, String& msg, bool relay) {
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(json);
    return onStop(root, msg, relay);
}

bool JsonProcessor::onStop(JsonObject& root, String& msg, bool relay) {
    RequestParameters params;
    JsonProcessor::parseRequestParams(root, params);
    app.rgbwwctrl.clearAnimationQueue(params.channels);
    app.rgbwwctrl.skipAnimation(params.channels);

    onDirect(root, msg, false);

    if (relay) {
        addChannelStatesToCmd(root, params.channels);
        app.onCommandRelay("stop", root);
    }

    return true;
}

bool JsonProcessor::onSkip(const String& json, String& msg, bool relay) {
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(json);
    return onSkip(root, msg, relay);
}

bool JsonProcessor::onSkip(JsonObject& root, String& msg, bool relay) {
    RequestParameters params;
    JsonProcessor::parseRequestParams(root, params);
    app.rgbwwctrl.skipAnimation(params.channels);

    onDirect(root, msg, false);

    if (relay) {
        addChannelStatesToCmd(root, params.channels);
        app.onCommandRelay("skip", root);
    }

    return true;
}

bool JsonProcessor::onPause(const String& json, String& msg, bool relay) {
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(json);
    return onPause(root, msg, relay);
}

bool JsonProcessor::onPause(JsonObject& root, String& msg, bool relay) {
    RequestParameters params;
    JsonProcessor::parseRequestParams(root, params);

    app.rgbwwctrl.pauseAnimation(params.channels);

    onDirect(root, msg, false);

    if (relay) {
        addChannelStatesToCmd(root, params.channels);
        app.onCommandRelay("pause", root);
    }

    return true;
}

bool JsonProcessor::onContinue(const String& json, String& msg, bool relay) {
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(json);
    return onContinue(root, msg, relay);
}

bool JsonProcessor::onContinue(JsonObject& root, String& msg, bool relay) {
    RequestParameters params;
    JsonProcessor::parseRequestParams(root, params);
    app.rgbwwctrl.continueAnimation(params.channels);

    if (relay)
        app.onCommandRelay("continue", root);

    return true;
}

bool JsonProcessor::onBlink(const String& json, String& msg, bool relay) {
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(json);
    return onBlink(root, msg, relay);
}

bool JsonProcessor::onBlink(JsonObject& root, String& msg, bool relay) {
    RequestParameters params;
    params.ramp.value = 500; //default

    JsonProcessor::parseRequestParams(root, params);

    app.rgbwwctrl.blink(params.channels, params.ramp.value, params.queue, params.requeue, params.name);

    if (relay)
        app.onCommandRelay("blink", root);

    return true;
}

bool JsonProcessor::onSingleColorCommand(JsonObject& root, String& errorMsg) {
    RequestParameters params;
    parseRequestParams(root, params);
    if (params.checkParams(errorMsg) != 0) {
        return false;
    }

    bool queueOk = false;
    if (params.mode == RequestParameters::Mode::Kelvin) {
        //TODO: hand to rgbctrl
    } else if (params.mode == RequestParameters::Mode::Hsv) {
        if(!params.hasHsvFrom) {
            if (params.cmd == "fade") {
                queueOk = app.rgbwwctrl.fadeHSV(params.hsv, params.ramp, params.direction, params.queue, params.requeue, params.name);
            } else {
                queueOk = app.rgbwwctrl.setHSV(params.hsv, params.ramp.value, params.queue, params.requeue, params.name);
            }
        } else {
            app.rgbwwctrl.fadeHSV(params.hsvFrom, params.hsv, params.ramp, params.direction, params.queue);
        }
    } else if (params.mode == RequestParameters::Mode::Raw) {
        if(!params.hasRawFrom) {
            if (params.cmd == "fade") {
                queueOk = app.rgbwwctrl.fadeRAW(params.raw, params.ramp, params.queue);
            } else {
                queueOk = app.rgbwwctrl.setRAW(params.raw, params.ramp.value, params.queue);
            }
        } else {
            app.rgbwwctrl.fadeRAW(params.rawFrom, params.raw, params.ramp, params.queue);
        }
    } else {
        errorMsg = "No color object!";
        return false;
    }

    if (!queueOk)
        errorMsg = "Queue full";
    return queueOk;
}

bool JsonProcessor::onDirect(const String& json, String& msg, bool relay) {
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(json);
    return onDirect(root, msg, relay);
}

bool JsonProcessor::onDirect(JsonObject& root, String& msg, bool relay) {
    RequestParameters params;
    JsonProcessor::parseRequestParams(root, params);

    if (params.mode == RequestParameters::Mode::Kelvin) {
        //TODO: hand to rgbctrl
    } else if (params.mode == RequestParameters::Mode::Hsv) {
        app.rgbwwctrl.colorDirectHSV(params.hsv);
    } else if (params.mode == RequestParameters::Mode::Raw) {
        app.rgbwwctrl.colorDirectRAW(params.raw);
    } else {
        msg = "No color object!";
    }

    if (relay)
        app.onCommandRelay("direct", root);

    return true;
}

void JsonProcessor::parseRequestParams(JsonObject& root, RequestParameters& params) {
    if (root["hsv"].success()) {
        params.mode = RequestParameters::Mode::Hsv;
        if (root["hsv"]["h"].success())
            params.hsv.h = AbsOrRelValue(root["hsv"]["h"].asString(), AbsOrRelValue::Type::Hue);
        if (root["hsv"]["s"].success())
            params.hsv.s = AbsOrRelValue(root["hsv"]["s"].asString());
        if (root["hsv"]["v"].success())
            params.hsv.v = AbsOrRelValue(root["hsv"]["v"].asString());
        if (root["hsv"]["ct"].success())
            params.hsv.ct = AbsOrRelValue(root["hsv"]["ct"].asString(), AbsOrRelValue::Type::Ct);

        if (root["hsv"]["from"].success()) {
            params.hasHsvFrom = true;
            if (root["hsv"]["from"]["h"].success())
                params.hsv.h = AbsOrRelValue(root["hsv"]["from"]["h"].asString(), AbsOrRelValue::Type::Hue);
            if (root["hsv"]["from"]["s"].success())
                params.hsv.s = AbsOrRelValue(root["hsv"]["from"]["s"].asString());
            if (root["hsv"]["from"]["v"].success())
                params.hsv.v = AbsOrRelValue(root["hsv"]["from"]["v"].asString());
            if (root["hsv"]["from"]["ct"].success())
                params.hsv.ct = AbsOrRelValue(root["hsv"]["from"]["ct"].asString(), AbsOrRelValue::Type::Ct);
        }
    }
    else if (root["raw"].success()) {
        params.mode = RequestParameters::Mode::Raw;
        if (root["raw"]["r"].success())
            params.raw.r = AbsOrRelValue(root["raw"]["r"].asString(), AbsOrRelValue::Type::Raw);
        if (root["raw"]["g"].success())
            params.raw.g = AbsOrRelValue(root["raw"]["g"].asString(), AbsOrRelValue::Type::Raw);
        if (root["raw"]["b"].success())
            params.raw.b = AbsOrRelValue(root["raw"]["b"].asString(), AbsOrRelValue::Type::Raw);
        if (root["raw"]["ww"].success())
            params.raw.ww = AbsOrRelValue(root["raw"]["ww"].asString(), AbsOrRelValue::Type::Raw);
        if (root["raw"]["cw"].success())
            params.raw.cw = AbsOrRelValue(root["raw"]["cw"].asString(), AbsOrRelValue::Type::Raw);

        if (root["raw"]["from"].success()) {
            params.hasRawFrom = true;
            if (root["raw"]["from"]["r"].success())
                params.rawFrom.r = AbsOrRelValue(root["raw"]["from"]["r"].asString(), AbsOrRelValue::Type::Raw);
            if (root["raw"]["from"]["g"].success())
                params.rawFrom.g = AbsOrRelValue(root["raw"]["from"]["g"].asString(), AbsOrRelValue::Type::Raw);
            if (root["raw"]["from"]["b"].success())
                params.rawFrom.b = AbsOrRelValue(root["raw"]["from"]["b"].asString(), AbsOrRelValue::Type::Raw);
            if (root["raw"]["from"]["ww"].success())
                params.rawFrom.ww = AbsOrRelValue(root["raw"]["from"]["ww"].asString(), AbsOrRelValue::Type::Raw);
            if (root["raw"]["from"]["cw"].success())
                params.rawFrom.cw = AbsOrRelValue(root["raw"]["from"]["cw"].asString(), AbsOrRelValue::Type::Raw);
        }
    }

    if (root["kelvin"].success()) {
        params.mode = RequestParameters::Mode::Kelvin;
    }

    if (root["t"].success()) {
        params.ramp.value = root["t"].as<double>();
        params.ramp.type = RampTimeOrSpeed::Type::Time;
    }

    if (root["s"].success()) {
        params.ramp.value = root["s"].as<double>();
        params.ramp.type = RampTimeOrSpeed::Type::Speed;
    }

    if (root["r"].success()) {
        params.requeue = root["r"].as<int>() == 1;
    }

    if (root["kelvin"].success()) {
        params.kelvin = root["kelvin"].as<int>();
    }

    if (root["d"].success()) {
        params.direction = root["d"].as<int>();
    }

    if (root["name"].success()) {
        params.name = root["name"].asString();
    }

    if (root["cmd"].success()) {
        params.cmd = root["cmd"].asString();
    }

    if (root["q"].success()) {
        const String& q = root["q"].asString();
        if (q == "back")
            params.queue = QueuePolicy::Back;
        else if (q == "front")
            params.queue = QueuePolicy::Front;
        else if (q == "front_reset")
            params.queue = QueuePolicy::FrontReset;
        else if (q == "single")
            params.queue = QueuePolicy::Single;
        else {
            params.queue = QueuePolicy::Invalid;
        }
    }

    if (root["channels"].success()) {
        const JsonArray& arr = root["channels"].asArray();
        for(size_t i=0; i < arr.size(); ++i) {
            const String& str = arr[i].asString();
            if (str == "h") {
                params.channels.add(CtrlChannel::Hue);
            }
            else if (str == "s") {
                params.channels.add(CtrlChannel::Sat);
            }
            else if (str == "v") {
                params.channels.add(CtrlChannel::Val);
            }
            else if (str == "ct") {
                params.channels.add(CtrlChannel::ColorTemp);
            }
        }
    }
}

int JsonProcessor::RequestParameters::checkParams(String& errorMsg) const {
    if (mode == Mode::Hsv) {
        if (hsv.ct.hasValue()) {
            if (hsv.ct != 0 && (hsv.ct < 100 || hsv.ct > 10000 || (hsv.ct > 500 && hsv.ct < 2000))) {
                errorMsg = "bad param for ct";
                return 1;
            }
        }

        if (!hsv.h.hasValue() && !hsv.s.hasValue() && !hsv.v.hasValue() && !hsv.ct.hasValue()) {
            errorMsg = "Need at least one HSVCT component!";
            return 1;
        }
    }
    else if (mode == Mode::Raw) {
        if (!raw.r.hasValue() && !raw.g.hasValue() && !raw.b.hasValue() && !raw.ww.hasValue() && !raw.cw.hasValue()) {
            errorMsg = "Need at least one RAW component!";
            return 1;
        }
    }

    if (queue == QueuePolicy::Invalid) {
        errorMsg = "Invalid queue policy";
        return 1;
    }

    if (cmd != "fade" && cmd != "solid") {
        errorMsg = "Invalid cmd";
        return 1;
    }

    if (direction < 0 || direction > 1) {
        errorMsg = "Invalid direction";
        return 1;
    }

    if (ramp.type == RampTimeOrSpeed::Type::Speed && ramp.value == 0) {
        errorMsg = "Speed cannot be 0!";
        return 1;
    }

    return 0;
}

bool JsonProcessor::onJsonRpc(const String& json) {
    debug_d("JsonProcessor::onJsonRpc: %s\n", json.c_str());
    JsonRpcMessageIn rpc(json);

    String msg;
    if (rpc.getMethod() == "color") {
        return onColor(rpc.getParams(), msg, false);
    }
    else if (rpc.getMethod() == "stop") {
        return onStop(rpc.getParams(), msg, false);
    }
    else if (rpc.getMethod() == "blink") {
        return onBlink(rpc.getParams(), msg, false);
    }
    else if (rpc.getMethod() == "skip") {
        return onSkip(rpc.getParams(), msg, false);
    }
    else if (rpc.getMethod() == "pause") {
        return onPause(rpc.getParams(), msg, false);
    }
    else if (rpc.getMethod() == "continue") {
        return onContinue(rpc.getParams(), msg, false);
    }
    else if (rpc.getMethod() == "direct") {
        return onDirect(rpc.getParams(), msg, false);
    }
}

void JsonProcessor::addChannelStatesToCmd(JsonObject& root, const RGBWWLed::ChannelList& channels) {
    switch(app.rgbwwctrl.getMode()) {
    case RGBWWLed::ColorMode::Hsv:
    {
        const HSVCT& c = app.rgbwwctrl.getCurrentColor();
        JsonObject& obj = root.createNestedObject("hsv");
        if (channels.count() == 0 || channels.contains(CtrlChannel::Hue))
            obj["h"] = (float(c.h) / float(RGBWW_CALC_HUEWHEELMAX)) * 360.0;
        if (channels.count() == 0 || channels.contains(CtrlChannel::Sat))
            obj["s"] = (float(c.s) / float(RGBWW_CALC_MAXVAL)) * 100.0;
        if (channels.count() == 0 || channels.contains(CtrlChannel::Val))
            obj["v"] = (float(c.v) / float(RGBWW_CALC_MAXVAL)) * 100.0;
        if (channels.count() == 0 || channels.contains(CtrlChannel::ColorTemp))
            obj["ct"] = c.ct;
        break;
    }
    case RGBWWLed::ColorMode::Raw:
    {
        const ChannelOutput& c = app.rgbwwctrl.getCurrentOutput();
        JsonObject& obj = root.createNestedObject("raw");
        if (channels.count() == 0 || channels.contains(CtrlChannel::Red))
            obj["r"] = c.r;
        if (channels.count() == 0 || channels.contains(CtrlChannel::Green))
            obj["g"] = c.g;
        if (channels.count() == 0 || channels.contains(CtrlChannel::Blue))
            obj["b"] = c.b;
        if (channels.count() == 0 || channels.contains(CtrlChannel::WarmWhite))
            obj["ww"] = c.ww;
        if (channels.count() == 0 || channels.contains(CtrlChannel::ColdWhite))
            obj["cw"] = c.cw;
        break;
    }
    }
}
