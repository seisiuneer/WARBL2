
#include <math.h>

//debug
void printStuff(void) {


    //Serial.println(nowtime - autoCenterYawTimer);
    //Serial.println(gyroX, 8);

    for (byte i = 0; i < 9; i++) {
        //Serial.println(toneholeRead[i]);
    }


    //Serial.println(toneholeRead[0]);

    //Serial.println(gyroX, 3);
    //Serial.println(gyroY, 3);
    //Serial.println(gyroZ, 3);
    //Serial.println(IMUsettings[mode][PITCH_REGISTER]);
    //Serial.println(IMUsettings[mode][PITCH_REGISTER_NUMBER]);
    //Serial.println(IMUsettings[2][PITCH_REGISTER_INPUT_MIN]);
    //Serial.println(IMUsettings[mode][PITCH_REGISTER_INPUT_MAX]);

    //Serial.println(digitalRead(STAT));
    //Serial.println(sensorValue);
    //Serial.println(word(readEEPROM(1013), readEEPROM(1014)));  //read the run time on battery since last full charge (minutes)
    //Serial.println(scalePosition);
    //Serial.println(prevRunTime);
    //Serial.println(fullRunTime);
    //Serial.println(CPUtemp, 2);
    //Serial.println(IMUtemp);
    // Serial.println(USBstatus);
    //Serial.println(battPower);
    //Serial.print(roll);
    //Serial.print(",");
    //Serial.println(modeSelector[0]);

    //Serial.println(holeCovered, BIN);
    //Serial.println(newState);
    //Serial.println(sensorCalibration);
    //Serial.println("");
}






//Read the pressure sensor and get latest tone hole readings from the ATmega.
void getSensors(void) {

    analogPressure.update();  // Reads the pressure sensor and applies adaptive filtering
    twelveBitPressure = analogPressure.getRawValue();

    // Use an adaptively smoothed 12-bit reading to map to CC, aftertouch, poly.
    smoothed_pressure = analogPressure.getValue();

    //Serial.println(smoothed_pressure);

    tempSensorValue = twelveBitPressure >> 2;  //Reduce the reading to stable 10 bits for state machine.

    //Receive tone hole readings from ATmega32U4. The transfer takes ~ 125 uS
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(2, LOW);   //SS -- wake up ATmega
    delayMicroseconds(10);  //Give it time to wake up.
    SPI.transfer(0);        //We don't receive anything useful back from the first transfer.
    for (byte i = 0; i < 12; i++) {
        toneholePacked[i] = SPI.transfer(i + 1);
    }
    digitalWrite(2, HIGH);  //SS
    SPI.endTransaction();


    //Unpack the readings from bytes to ints.
    for (byte i = 0; i < 9; i++) {
        toneholeRead[i] = toneholePacked[i];  // Unpack lower 8 bits.
    }

    for (byte i = 0; i < 4; i++) {  //Unpack the upper 2 bits of holes 0-3.
        toneholePacked[9] = toneholePacked[9] & 0b11111111, BIN;
        toneholeRead[i] = toneholeRead[i] | (toneholePacked[9] << 2) & 0b1100000000;
        toneholePacked[9] = toneholePacked[9] << 2;
    }

    for (byte i = 4; i < 8; i++) {  //Unpack the upper 2 bits of holes 4-7.
        toneholePacked[10] = toneholePacked[10] & 0b11111111, BIN;
        toneholeRead[i] = toneholeRead[i] | (toneholePacked[10] << 2) & 0b1100000000;
        toneholePacked[10] = toneholePacked[10] << 2;
    }

    toneholeRead[8] = toneholeRead[8] | (toneholePacked[11] << 8);  //Unpack the upper 2 bits of hole 8.



    for (byte i = 0; i < 9; i++) {
        if (calibration == 0) {  //If we're not calibrating, compensate for baseline sensor offset (the stored sensor reading with the hole completely uncovered).
            toneholeRead[i] = toneholeRead[i] - toneholeBaseline[i];
        }
        if (toneholeRead[i] < 0) {  //In rare cases the adjusted readings might end up being negative.
            toneholeRead[i] = 0;
        }
    }
}









void readIMU(void) {


    sensors_event_t accel;
    sensors_event_t gyro;
    sensors_event_t temp;
    sox.getEvent(&accel, &gyro, &temp);

    rawGyroX = gyro.gyro.x;
    rawGyroY = gyro.gyro.y;
    rawGyroZ = gyro.gyro.z;
    accelX = accel.acceleration.x;
    accelY = accel.acceleration.y;
    accelZ = accel.acceleration.z;
    IMUtemp = temp.temperature;

    //calibrate gyro
    gyroX = rawGyroX - gyroXCalibration;
    gyroY = rawGyroY - gyroYCalibration;
    gyroZ = rawGyroZ - gyroZCalibration;


    float deltat = sfusion.deltatUpdate();
    deltat = constrain(deltat, 0.001f, 0.01f);  // just to keep it from freaking out, consider just giving it a fixed deltat in future
    //AM 9/16/23 Enabled DWT to make micros() higher resolution for this calculation. Not sure that it matters.

    //Serial.println(deltat, 4);

    //sfusion.MadgwickUpdate(gyroX, gyroY, gyroZ, accelX, accelY, accelZ, deltat);
    sfusion.MahonyUpdate(gyroX, gyroY, gyroZ, accelX, accelY, accelZ, deltat);

    // pitch and roll are swapped due to PCB sensor orientation
    pitch = sfusion.getRollRadians();
    roll = sfusion.getPitchRadians();
    yaw = sfusion.getYawRadians();


#if 0
    float * quat = sfusion.getQuat();
    for (int i=0; i < 4; ++i) {
        Serial.print(quat[i], 4);
        Serial.print(", ");
    }
    Serial.println(" 1.1, -1.1");
#endif


    currYaw = yaw;  // needs to be the unadjusted value

    yaw += yawOffset;
    if (yaw > PI) yaw -= TWO_PI;
    else if (yaw < -PI) yaw += TWO_PI;



    // adjust pitch so it makes more sense for way warbl is held, shift it 180 deg
    pitch += PI;
    if (pitch > PI) pitch -= TWO_PI;

    // invert all three
    roll = -roll;
    pitch = -pitch;
    yaw = -yaw;


    roll = roll * RAD_TO_DEG;
    pitch = pitch * RAD_TO_DEG;
    yaw = yaw * RAD_TO_DEG;



    /*
    //Failed experiment integrating gyroY without accelerometer for roll in the local frame. 
    static float rollLocal = roll; //Initialize using global frame (not sure this is any better than initializing to zero).
    rollLocal += ((gyroY * RAD_TO_DEG) * deltat);  //Integrate
    if (abs(roll) < 0.5 && abs(pitch) < 60) {                           //If roll in the global frame is close to zero, zero the local roll as well. Doing this at high pitch can cause artifacts, so limit to low pitch.
        rollLocal = 0;                                                  //It would be better to use a correction factor to "nudge" the zero point to the correct location rather than doing it abruptly, unless perhaps it's far out of range (like after powerup).
    }

    //Serial.println(rollLocal, 0);

    //comment out this line to switch back to global frame for roll.
    roll = rollLocal;
   */
}







//Calibrate the IMU when the command is received from the Config Tool.
void calibrateIMU() {

    sox.setGyroDataRate(LSM6DS_RATE_208_HZ);  //Make sure the gyro is on.
    delay(5);                                 //Give it a bit of time.
    readIMU();                                //Get a reading in case we haven't been reading it.

    gyroXCalibration = rawGyroX;
    gyroYCalibration = rawGyroY;
    gyroZCalibration = rawGyroZ;

    putEEPROM(985, gyroXCalibration);  //Put the current readings into EEPROM.
    putEEPROM(989, gyroYCalibration);
    putEEPROM(993, gyroZCalibration);
}








// reset heading 0 to current heading
void centerIMU() {
    yawOffset = -currYaw;
}







//Map IMU to CC and send
void sendIMU() {

    static byte prevRollCC = 0;
    static byte prevPitchCC = 0;
    static byte prevYawCC = 0;

    //The min and max settings from the Config Tool range from 0-36 and are scaled up to the maximum range of angles for each DOF.

    if (IMUsettings[mode][SEND_ROLL]) {

        byte lowerConstraint;
        byte upperConstraint;

        if (IMUsettings[mode][ROLL_OUTPUT_MIN] > IMUsettings[mode][ROLL_OUTPUT_MAX]) {  //Flip the constraints if the lower output is greater than the upper output, so the output can be inverted.
            lowerConstraint = IMUsettings[mode][ROLL_OUTPUT_MAX];
            upperConstraint = IMUsettings[mode][ROLL_OUTPUT_MIN];
        }

        else {
            lowerConstraint = IMUsettings[mode][ROLL_OUTPUT_MIN];
            upperConstraint = IMUsettings[mode][ROLL_OUTPUT_MAX];
        }

        byte rollOutput = constrain(map((roll + 90) * 1000, IMUsettings[mode][ROLL_INPUT_MIN] * 5000, IMUsettings[mode][ROLL_INPUT_MAX] * 5000, IMUsettings[mode][ROLL_OUTPUT_MIN], IMUsettings[mode][ROLL_OUTPUT_MAX]), lowerConstraint, upperConstraint);
        //Serial.println(pitchOutput);

        if (prevRollCC != rollOutput) {
            sendMIDI(CC, IMUsettings[mode][ROLL_CC_CHANNEL], IMUsettings[mode][ROLL_CC_NUMBER], rollOutput);
            prevRollCC = rollOutput;
        }
    }



    if (IMUsettings[mode][SEND_PITCH]) {

        byte lowerConstraint;
        byte upperConstraint;

        if (IMUsettings[mode][PITCH_OUTPUT_MIN] > IMUsettings[mode][PITCH_OUTPUT_MAX]) {  //Flip the constraints if the lower output is greater than the upper output, so the output can be inverted.
            lowerConstraint = IMUsettings[mode][PITCH_OUTPUT_MAX];
            upperConstraint = IMUsettings[mode][PITCH_OUTPUT_MIN];
        }

        else {
            lowerConstraint = IMUsettings[mode][PITCH_OUTPUT_MIN];
            upperConstraint = IMUsettings[mode][PITCH_OUTPUT_MAX];
        }

        byte pitchOutput = constrain(map((pitch + 90) * 1000, IMUsettings[mode][PITCH_INPUT_MIN] * 5000, IMUsettings[mode][PITCH_INPUT_MAX] * 5000, IMUsettings[mode][PITCH_OUTPUT_MIN], IMUsettings[mode][PITCH_OUTPUT_MAX]), lowerConstraint, upperConstraint);
        //Serial.println(pitchOutput);

        if (prevPitchCC != pitchOutput) {
            sendMIDI(CC, IMUsettings[mode][PITCH_CC_CHANNEL], IMUsettings[mode][PITCH_CC_NUMBER], pitchOutput);
            prevPitchCC = pitchOutput;
        }
    }



    if (IMUsettings[mode][SEND_YAW]) {

        byte lowerConstraint;
        byte upperConstraint;

        if (IMUsettings[mode][YAW_OUTPUT_MIN] > IMUsettings[mode][YAW_OUTPUT_MAX]) {  //Flip the constraints if the lower output is greater than the upper output, so the output can be inverted.
            lowerConstraint = IMUsettings[mode][YAW_OUTPUT_MAX];
            upperConstraint = IMUsettings[mode][YAW_OUTPUT_MIN];
        }

        else {
            lowerConstraint = IMUsettings[mode][YAW_OUTPUT_MIN];
            upperConstraint = IMUsettings[mode][YAW_OUTPUT_MAX];
        }

        byte yawOutput = constrain(map((yaw + 180) * 1000, IMUsettings[mode][YAW_INPUT_MIN] * 10000, IMUsettings[mode][YAW_INPUT_MAX] * 10000, IMUsettings[mode][YAW_OUTPUT_MIN], IMUsettings[mode][YAW_OUTPUT_MAX]), lowerConstraint, upperConstraint);
        //Serial.println(yawOutput);

        if (prevYawCC != yawOutput) {
            sendMIDI(CC, IMUsettings[mode][YAW_CC_CHANNEL], IMUsettings[mode][YAW_CC_NUMBER], yawOutput);
            prevYawCC = yawOutput;
        }
    }
}










void shakeForVibrato() {

    if (IMUsettings[mode][Y_SHAKE_PITCHBEND]) {

        static float accelFilteredOld;
        const float timeConstant = 0.1f;

        float accelFiltered = timeConstant * accelY + (1.0f - timeConstant) * accelFilteredOld;  // Low-pass filter to isolate gravity from the Y accelerometer axis.
        accelFilteredOld = accelFiltered;
        float highPassY = accelY - accelFiltered;  // Subtract gravity to high-pass Y.


        // A second low pass filter to minimize spikes from tapping the tone holes.
        static float accelFilteredBOld;

        const float timeConstant2 = 0.5f;
        float accelFilteredB = timeConstant2 * highPassY + (1.0f - timeConstant2) * accelFilteredBOld;
        float lastFilteredB = accelFilteredBOld;
        accelFilteredBOld = accelFilteredB;

        //accelFilteredB = highPassY;  // (Don't) temporarily eliminate this lowpass to see if speeds up response noticeably.

        const float shakeBendDepth = 4.0f * IMUsettings[mode][Y_PITCHBEND_DEPTH] / 100;  // Adjust the vibrato depth range based on the Config Tool setting.

        const float kShakeStartThresh = 0.5f;
        const float kShakeFinishThresh = 0.35f;
        const long kShakeFinishTimeMs = 400;
        const long ktapFilterTimeMs = 12;  //ms window for further filtering out taps on the tone holes

        static bool shakeActive = false;
        static bool tapFilterActive = false;
        static long lastThreshExceedTime = 0;
        static long tapFilterStartTime = 0;
        static bool preTrigger = false;

        shakeVibrato = 0;


        if (tapFilterActive == false && abs(accelFilteredB) > kShakeStartThresh) {
            tapFilterActive = true;
            tapFilterStartTime = nowtime;
            // Serial.println("Shake Tap Filt");
        }


        if ((nowtime - tapFilterStartTime) < ktapFilterTimeMs) {  //Return if we haven't waited long enough after crossing the shake threshold, for further filtering brief tone hole taps.
            return;
        }


        if (!preTrigger && abs(accelFilteredB) > kShakeStartThresh) {
            preTrigger = true;
            // Serial.println("Pre Shake Trigger");
        }

        // start shake vib only when doing a zero crossing after threshold has been triggered
        if (!shakeActive && preTrigger
            // comment out next line to test without the zero crossing requirement
            //&& ((lastFilteredB <= 0.0f && accelFilteredB > 0.0f) || (lastFilteredB > 0.0f && accelFilteredB <= 0.0f))
        ) {
            shakeActive = true;
            lastThreshExceedTime = nowtime;
            // Serial.println("Start ShakeVib");
        } else if (abs(accelFilteredB) > kShakeFinishThresh) {
            lastThreshExceedTime = nowtime;
        }

        if ((shakeActive || tapFilterActive) && (nowtime - lastThreshExceedTime) > kShakeFinishTimeMs) {
            // Stop shake vibrato.
            shakeActive = false;
            tapFilterActive = false;
            preTrigger = false;
            // Serial.println("Cancel ShakeVib");
        }

        if (shakeActive) {
            // Normalize and clip, +/-15 input seems to be reasonably realistic max accel while still having it in the mouth!
            float normshake = constrain(accelFilteredB * 0.06666f, -1.0f, 1.0f);
            if (IMUsettings[mode][Y_PITCHBEND_MODE] == Y_PITCHBEND_MODE_UPDOWN) {
                normshake *= -1.0f;  // reverse phase
            } else if (IMUsettings[mode][Y_PITCHBEND_MODE] == Y_PITCHBEND_MODE_DOWNONLY) {
                normshake = constrain(normshake, -1.0f, 0.0f);
            } else if (IMUsettings[mode][Y_PITCHBEND_MODE] == Y_PITCHBEND_MODE_UPONLY) {
                normshake = constrain(-1.0f * normshake, 0.0f, 1.0f);
            }

            shakeVibrato = normshake * shakeBendDepth * pitchBendPerSemi;

            //Serial.print(1.0f);
            //Serial.print(",");
            //Serial.print(normshake);
            //Serial.print(",");
            //Serial.println(-1.0f);
        }

        if (pitchBendMode == kPitchBendNone) {  // If we don't have finger vibrato and/or slide turned on, we need to send the pitchbend now.
            sendPitchbend();
        }
    }
}









//Return number of registers to jump based on IMU pitch.
int pitchRegister() {

    for (byte i = 1; i < IMUsettings[mode][PITCH_REGISTER_NUMBER] + 1; i++) {
        if (pitch < pitchRegisterBounds[i] || (i == IMUsettings[mode][PITCH_REGISTER_NUMBER] && pitch >= pitchRegisterBounds[i])) {  //See if IMU pitch is within the bounds for each register.
            return i - 1;
        }
    }
    return 0;
}








//Monitor the status of the 3 buttons. The integrating debouncing algorithm is taken from debounce.c, written by Kenneth A. Kuhn:http://www.kennethkuhn.com/electronics/debounce.c
void checkButtons() {


    for (byte j = 0; j < 3; j++) {


        if (digitalRead(buttons[j]) == 0) {  //if the button reads low, reduce the integrator by 1
            if (integrator[j] > 0) {
                integrator[j]--;
            }
        } else if (integrator[j] < MAXIMUM) {  //if the button reads high, increase the integrator by 1
            integrator[j]++;
        }


        if (integrator[j] == 0) {  //the button is pressed.
            pressed[j] = 1;        //we make the output the inverse of the input so that a pressed button reads as a "1".
            //buttonUsed = 1;        //flag that there's been button activity, so we know to handle it.

            if (prevOutput[j] == 0 && !longPressUsed[j]) {
                justPressed[j] = 1;  //the button has just been pressed
            }

            else {
                justPressed[j] = 0;
            }

            if (prevOutput[j] == 1) {  //increase a counter so we know when a button has been held for a long press
                longPressCounter[j]++;
            }
        }



        else if (integrator[j] >= MAXIMUM) {  //the button is not pressed
            pressed[j] = 0;
            integrator[j] = MAXIMUM;  // defensive code if integrator got corrupted

            if (prevOutput[j] == 1 && !longPressUsed[j]) {
                released[j] = 1;  //the button has just been released
                buttonUsed = 1;
            }

            longPress[j] = 0;
            longPressUsed[j] = 0;  //if a button is not pressed, reset the flag that tells us it's been used for a long press.
            longPressCounter[j] = 0;
        }



        if (longPressCounter[j] > 300 && !longPressUsed[j]) {  //if the counter gets to a certain level, it's a long press
            longPress[j] = 1;
            buttonUsed = 1;
        }


        prevOutput[j] = pressed[j];  //keep track of state for next time around.
    }

    if (nowtime < 1000) {  //ignore button 3 for the first bit after powerup in case it was only being used to power on the device.
        pressed[2] = 0;
        released[2] = 0;
    }
}









//Determine which holes are covered
void get_fingers() {

    for (byte i = 0; i < 9; i++) {
        if ((toneholeRead[i]) > (toneholeCovered[i] - 50)) {
            bitWrite(holeCovered, i, 1);  //use the tonehole readings to decide which holes are covered
        } else if ((toneholeRead[i]) <= (toneholeCovered[i] - 54)) {
            bitWrite(holeCovered, i, 0);  //decide which holes are uncovered -- the "hole uncovered" reading is a little less then the "hole covered" reading, to prevent oscillations.
        }
    }
}








//Key delay feature for delaying response to tone holes and filtering out transient notes, originally by Louis Barman
bool debounceFingerHoles() {

    static unsigned long debounceTimer;
    unsigned long now = millis();
    static bool timing;

    if (prevHoleCovered != holeCovered) {
        prevHoleCovered = holeCovered;
        debounceTimer = now;
        timing = 1;
    }

    if (now - debounceTimer >= transientFilter && timing == 1) {
        timing = 0;
        return true;
    }

    return false;
}









//Send the finger pattern and pressure to the Configuration Tool after a delay to prevent sending during the same connection interval as a new MIDI note.
void sendToConfig(bool newPattern, bool newPressure) {

    static bool patternChanged = false;
    static bool pressureChanged = false;
    static unsigned long patternSendTimer;
    static unsigned long pressureSendTimer;

    if (communicationMode) {
        if (newPattern && patternChanged == false) {  //If the fingering pattern has changed, start a timer.
            patternChanged = true;
            patternSendTimer = nowtime;
        }

        if (newPressure && pressureChanged == false) {  //If the pressure has changed, start a timer.
            pressureChanged = true;
            pressureSendTimer = nowtime;
        }

        if (patternChanged && (nowtime - patternSendTimer) > 25) {  //If some time has past, send the new pattern to the Config Tool.
            sendMIDI(CC, 7, 114, holeCovered >> 7);                 //Because it's MIDI we have to send it in two 7-bit chunks.
            sendMIDI(CC, 7, 115, lowByte(holeCovered));
            patternChanged = false;
        }

        if (pressureChanged && (nowtime - pressureSendTimer) > 25) {  //If some time has past, send the new pressure to the Config Tool.
            sendMIDI(CC, 7, 116, sensorValue & 0x7F);                 //Send LSB of current pressure to Configuration Tool.
            sendMIDI(CC, 7, 118, sensorValue >> 7);                   //Send MSB of current pressure.
            pressureChanged = false;
        }
    }
}








//Return a MIDI note number (0-127) based on the current fingering.
int get_note(unsigned int fingerPattern) {
    int ret = -1;  //default for unknown fingering

    switch (modeSelector[mode]) {  //determine the note based on the fingering pattern


        case kModeWhistle:
            {
            }  //these first two are the same

        case kModeChromatic:
            {
            }

        case kModeBombarde:
            {
            }  //this one is very similar-- a modification will be made below.

        case kModeBaroqueFlute:
            {  //this one is very similar-- a modification will be made below.

                tempCovered = (0b011111100 & fingerPattern) >> 2;  //use bitmask and bitshift to ignore thumb sensor, R4 sensor, and bell sensor when using tinwhistle fingering. The R4 value will be checked later to see if a note needs to be flattened.
                ret = tinwhistle_explicit[tempCovered].midi_note;
                if (modeSelector[mode] == kModeChromatic && bitRead(fingerPattern, 1) == 1) {
                    ret--;  //lower a semitone if R4 hole is covered and we're using the chromatic pattern
                }
                if (modeSelector[mode] == kModeBaroqueFlute) {
                    if ((0b011111110 & fingerPattern) >> 2 == 0b010011) {
                        ret = 75;
                    }
                    if ((0b011111110 & fingerPattern) >> 2 == 0b110011) {
                        ret = 76;
                    }
                    if (bitRead(fingerPattern, 1) == 1) {
                        ret++;  //raise a semitone if R4 hole is covered and we're using the baroque pattern, imitating a D# key
                    }
                }

                if (fingerPattern == holeCovered) {
                    vibratoEnable = tinwhistle_explicit[tempCovered].vibrato;
                }
                if (modeSelector[mode] == kModeBombarde) {
                    if ((0b011111110 & fingerPattern) >> 1 == 0b1111111) {
                        ret = 61;  //
                    }
                    if ((0b011111110 & fingerPattern) >> 1 == 0b0100000) {
                        ret = 72;  //
                    }
                }
                return ret;
            }


        case kModeUilleann:
            {
            }  //these two are the same, with the exception of cancelling accidentals.

        case kModeUilleannStandard:
            {  //the same as the previous one, but we cancel the accidentals Bb and F#


                //If back D open, always play the D and allow finger vibrato
                if ((fingerPattern & 0b100000000) == 0) {
                    if (fingerPattern == holeCovered) {
                        vibratoEnable = 0b000010;
                    }
                    return 74;
                }
                tempCovered = (0b011111110 & fingerPattern) >> 1;  //ignore thumb hole and bell sensor
                ret = uilleann_explicit[tempCovered].midi_note;
                if (fingerPattern == holeCovered) {
                    vibratoEnable = uilleann_explicit[tempCovered].vibrato;
                }
                if (modeSelector[mode] == kModeUilleannStandard) {  //cancel accidentals if we're in standard uilleann mode
                    if (tempCovered == 0b1001000 || tempCovered == 0b1001010) {
                        return 71;
                    }

                    if (tempCovered == 0b1101000 || tempCovered == 0b1101010) {
                        return 69;
                    }
                }
                return ret;
            }


        case kModeGHB:
            {

                //If back A open, always play the A
                if ((fingerPattern & 0b100000000) == 0) {
                    return 74;
                }
                tempCovered = (0b011111110 & fingerPattern) >> 1;  //ignore thumb hole and bell sensor
                ret = GHB_explicit[tempCovered].midi_note;
                return ret;
            }


        case kModeRecorder:
            {  //this is especially messy, should be cleaned up

                //If back thumb and L1 are open
                if ((((fingerPattern & 0b110000000) == 0) && (fingerPattern & 0b011111111) != 0) && (fingerPattern >> 1) != 0b00101100) {
                    if (fingerPattern >> 1 == 0b00110000) {  //special fingering for D#
                        return 77;
                    } else {
                        return 76  //play D
                          ;
                    }
                }
                if (fingerPattern >> 1 == 0b01011010) return 88;  //special fingering for high D
                if (fingerPattern >> 1 == 0b01001100) return 86;  //special fingering for high C
                //otherwise check the chart.
                tempCovered = (0b011111110 & fingerPattern) >> 1;  //ignore thumb hole and bell sensor
                ret = recorder_explicit[tempCovered].midi_note;
                //If back thumb is open
                if ((fingerPattern & 0b100000000) == 0 && (fingerPattern >> 1) != 0b00101100) {
                    ret = ret + 12;
                }
                return ret;
            }


        case kModeNorthumbrian:
            {

                //If back A open, always play the A
                if ((fingerPattern & 0b100000000) == 0) {
                    return 74;
                }
                tempCovered = fingerPattern >> 1;                 //bitshift once to ignore bell sensor reading
                tempCovered = findleftmostunsetbit(tempCovered);  //here we find the index of the leftmost uncovered hole, which will be used to determine the note from the general chart.
                for (uint8_t i = 0; i < 9; i++) {
                    if (tempCovered == northumbrian_general[i].keys) {
                        ret = northumbrian_general[i].midi_note;
                        return ret;
                    }
                }
                break;
            }

        case kModeGaita:
            {

                tempCovered = fingerPattern >> 1;  //bitshift once to ignore bell sensor reading
                ret = gaita_explicit[tempCovered].midi_note;
                if (ret == 0) {
                    ret = -1;
                }
                return ret;
            }


        case kModeGaitaExtended:
            {

                tempCovered = fingerPattern >> 1;  //bitshift once to ignore bell sensor reading
                ret = gaita_extended_explicit[tempCovered].midi_note;
                if (ret == 0) {
                    ret = -1;
                }
                return ret;
            }


        case kModeNAF:
            {

                tempCovered = (0b011111110 & fingerPattern) >> 1;  //ignore thumb hole and bell sensor
                ret = naf_explicit[tempCovered].midi_note;
                return ret;
            }


        case kModeEVI:
            {

                tempCovered = (0b011111110 & fingerPattern) >> 1;  //ignore thumb hole and bell sensor
                ret = evi_explicit[tempCovered].midi_note;
                ret = ret + 4;  //transpose up to D so that key selection in the Configuration Tool works properly
                return ret;
            }


        case kModeKaval:
            {

                //If back thumb is open, always play the B
                if ((fingerPattern & 0b100000000) == 0) {
                    return 71;
                }
                tempCovered = (0b011111110 & fingerPattern) >> 1;  //ignore thumb hole and bell sensor
                ret = kaval_explicit[tempCovered].midi_note;
                return ret;
            }


        case kModeXiao:
            {

                //Catch a few specific patterns with the thumb hole open:
                if ((fingerPattern & 0b100000000) == 0) {
                    if ((fingerPattern >> 4) == 0b01110) {  //if the top 5 holes are as shown
                        return 70;                          //play Bb
                    }
                    if ((fingerPattern & 0b010000000) == 0) {  //if hole L1 is also open
                        return 71;                             //play B
                    }
                    return 72;  //otherwise play a C
                } else if ((fingerPattern & 0b010000000) == 0) {
                    return 69;  //if thumb is closed but L1 is open play an A
                }
                //otherwise check the chart.
                tempCovered = (0b001111110 & fingerPattern) >> 1;  //ignore thumb hole, L1 hole, and bell sensor
                ret = xiao_explicit[tempCovered].midi_note;
                return ret;
            }


        case kModeSax:
            {

                //check the chart.
                tempCovered = (0b011111100 & fingerPattern) >> 2;  //ignore thumb hole, R4 hole, and bell sensor
                ret = sax_explicit[tempCovered].midi_note;
                if (((fingerPattern & 0b000000010) != 0) && ret != 47 && ret != 52 && ret != 53 && ret != 54 && ret != 59 && ret != 60 && ret != 61) {  //sharpen the note if R4 is covered and the note isn't one of the ones that can't be sharpened (a little wonky but works and keep sthe chart shorter ;)
                    ret++;
                }
                if ((fingerPattern & 0b100000000) != 0 && ret > 49) {  //if the thumb hole is covered, raise the octave
                    ret = ret + 12;
                }
                return ret;
            }


        case kModeSaxBasic:
            {

                //check the chart.
                tempCovered = (0b011111110 & fingerPattern) >> 1;  //ignore thumb hole and bell sensor
                ret = saxbasic_explicit[tempCovered].midi_note;
                if ((fingerPattern & 0b100000000) != 0 && ret > 49) {  //if the thumb hole is covered, raise the octave
                    ret = ret + 12;
                }
                return ret;
            }


        case kModeShakuhachi:
            {

                //ignore all unused holes by extracting bits and then logical OR
                {
                    //braces necessary for scope
                    byte high = (fingerPattern >> 4) & 0b11000;
                    byte middle = (fingerPattern >> 4) & 0b00011;
                    middle = middle << 1;
                    byte low = (fingerPattern >> 2) & 0b0000001;
                    tempCovered = high | middle;
                    tempCovered = tempCovered | low;
                    ret = shakuhachi_explicit[tempCovered].midi_note;
                    return ret;
                }
            }


        case kModeSackpipaMajor:
            {
            }

        case kModeSackpipaMinor:
            {  //the same except we'll change C# to C

                //check the chart.
                tempCovered = (0b011111100 & fingerPattern) >> 2;        //ignore thumb hole, R4 hole, and bell sensor
                if ((fingerPattern & 0b111111110) >> 1 == 0b11111111) {  //play D if all holes are covered
                    return 60;                                           //play D
                }
                if ((fingerPattern & 0b100000000) == 0) {  //if the thumb hole is open, play high E
                    return 74;                             //play E
                }
                ret = sackpipa_explicit[tempCovered].midi_note;
                if (modeSelector[mode] == kModeSackpipaMinor) {  //flatten the C# if we're in "minor" mode
                    if (ret == 71) {
                        return 70;  //play C natural instead
                    }
                }
                return ret;
            }



        case kModeMedievalPipes:
            {
                //If back A open, always play the A
                if ((fingerPattern & 0b100000000) == 0) {
                    if ((fingerPattern & 0b010000000) == 0) {
                        return 76;  //A if thumb and L1 are open
                    }
                    return 78;  //Bb if only thumb is open
                }
                tempCovered = (0b011111110 & fingerPattern) >> 1;  //ignore thumb hole and bell sensor
                ret = medievalPipes_explicit[tempCovered].midi_note;
                return ret;
            }



        case kModeBansuriWARBL:
            {
            }

        case kModeBansuri:  //ToDO: Make sure this matches the old firmware (2.2)
            {

                //check the chart.
                tempCovered = (0b011111100 & fingerPattern) >> 2;  //ignore thumb hole, R4 hole, and bell sensor
                ret = bansuri_explicit[tempCovered].midi_note;
                if ((fingerPattern & 0b100000000) == 0) {  //if the thumb hole is open, play G
                    ret = 74;
                } else if ((fingerPattern & 0b111111110) >> 1 == 0b11111111) {  //play F# if all holes are covered
                    ret = 61;
                }


                if (modeSelector[mode] == kModeBansuriWARBL) {
                    if (fingerPattern >> 1 != 0b11111111 && (bitRead(fingerPattern, 1) == 1)) {  //if R4 is covered and we're not playing an F#, raise the note one semitone
                        ret--;
                    }
                }
                return ret;
            }



        case kWARBL2Custom1:  //If we're using a custom chart we've already loaded the currently selected one from EEPROM into the array, so these can all fall through here.
            {
            }
        case kWARBL2Custom2:
            {
            }
        case kWARBL2Custom3:
            {
            }
        case kWARBL2Custom4:
            {
                tempCovered = fingerPattern >> 1;  //bitshift once to ignore bell sensor reading
                ret = WARBL2CustomChart[tempCovered];
                return ret;
            }


        default:
            {
                return ret;
            }
    }

    return ret;
}







//Add up any transposition based on key and register.
void get_shift() {

    byte pitchShift;

    if (IMUsettings[mode][PITCH_REGISTER] == true) {
        pitchShift = pitchRegister();
    }


    shift = ((octaveShift * 12) + noteShift + (pitchShift * 12));  //adjust for key and octave shift.

    if (newState == 3 && !(modeSelector[mode] == kModeEVI || (modeSelector[mode] == kModeSax && newNote < 62) || (modeSelector[mode] == kModeSaxBasic && newNote < 74) || (modeSelector[mode] == kModeRecorder && newNote < 76)) && !(newNote == 62 && (modeSelector[mode] == kModeUilleann || modeSelector[mode] == kModeUilleannStandard))) {  //if overblowing (except EVI, sax in the lower register, and low D with uilleann fingering, which can't overblow)
        shift = shift + 12;                                                                                                                                                                                                                                                                                                                      //add a register jump to the transposition if overblowing.
        if (modeSelector[mode] == kModeKaval) {                                                                                                                                                                                                                                                                                                  //Kaval only plays a fifth higher in the second register.
            shift = shift - 5;
        }
    }

    if (breathMode == kPressureBell && modeSelector[mode] != kModeUilleann && modeSelector[mode] != kModeUilleannStandard) {  //if we're using the bell sensor to control register
        if (bitRead(holeCovered, 0) == switches[mode][INVERT]) {
            shift = shift + 12;  //add a register jump to the transposition if necessary.
            if (modeSelector[mode] == kModeKaval) {
                shift = shift - 5;
            }
        }
    }

    else if ((breathMode == kPressureThumb && (modeSelector[mode] == kModeWhistle || modeSelector[mode] == kModeChromatic || modeSelector[mode] == kModeNAF))) {  //if we're using the left thumb to control the regiser with a fingering patern that doesn't normally use the thumb

        if (bitRead(holeCovered, 8) == switches[mode][INVERT]) {
            shift = shift + 12;  //add an octave jump to the transposition if necessary.
        }
    }

    //Some charts require another transposition to bring them to the correct key
    if (modeSelector[mode] == kModeGaita || modeSelector[mode] == kModeGaitaExtended) {
        shift = shift - 1;
    }

    if (modeSelector[mode] == kModeSax) {
        shift = shift + 2;
    }

    if (modeSelector[mode] == kModeBansuri || modeSelector[mode] == kModeBansuriWARBL) {
        shift = shift - 5;
    }
}









//State machine that models the way that a tinwhistle etc. begins sounding and jumps octaves in response to breath pressure.
//The current jump/drop behavior is from Louis Barman
void get_state() {
    sensorValue2 = tempSensorValue;  //Transfer last reading.

    byte scalePosition;  //ScalePosition is used to tell where we are on the scale, because higher notes are more difficult to overblow.

    if (modeSelector[mode] == kWARBL2Custom1 || modeSelector[mode] == kWARBL2Custom2 || modeSelector[mode] == kWARBL2Custom3 || modeSelector[mode] == kWARBL2Custom4) {
        scalePosition = findleftmostunsetbit(holeCovered) + 62;
        if (scalePosition > 69) {
            scalePosition = 70;
        }
    }

    else {
        scalePosition = newNote;
    }

    if (ED[mode][DRONES_CONTROL_MODE] == 3) {  //Use pressure to control drones if that option has been selected. There's a small amount of hysteresis added.

        if (!dronesOn && sensorValue2 > 5 + (ED[mode][DRONES_PRESSURE_HIGH_BYTE] << 7 | ED[mode][DRONES_PRESSURE_LOW_BYTE])) {
            startDrones();
        }

        else if (dronesOn && sensorValue2 < (ED[mode][DRONES_PRESSURE_HIGH_BYTE] << 7 | ED[mode][DRONES_PRESSURE_LOW_BYTE])) {
            stopDrones();
        }
    }

    upperBound = (sensorThreshold[1] + ((scalePosition - 60) * multiplier));  //Calculate the threshold between state 2 (bottom register) and state 3 (top register). This will also be used to calculate expression.


    newState = currentState;

    if (sensorValue2 <= sensorThreshold[0]) {
        newState = SILENCE;
        holdoffActive = false;  //No need to wait for jump/drop if we've already crossed the threshold for silence
    } else if (sensorValue2 > sensorThreshold[0] + SILENCE_HYSTERESIS) {
        if (currentState == SILENCE) {
            newState = BOTTOM_REGISTER;
        }


        if (breathMode == kPressureBreath) {  //If overblowing is enabled
            upperBoundHigh = calcHysteresis(upperBound, true);
            upperBoundLow = calcHysteresis(upperBound, false);
            if (sensorValue2 > upperBoundHigh) {
                newState = TOP_REGISTER;
                holdoffActive = false;
            } else if (sensorValue2 <= upperBoundLow) {
                newState = BOTTOM_REGISTER;
            }

            //Wait to decide about jump or drop if necessary.
            if (currentState == SILENCE && newState == BOTTOM_REGISTER) {
                newState = delayStateChange(JUMP, sensorValue2, upperBoundHigh);
            } else if (currentState == TOP_REGISTER && newState == BOTTOM_REGISTER && (millis() - fingeringChangeTimer) > 20) {  //Only delay for drop if the note has been playing for a bit. This fixes erroneous high-register notes.
                newState = delayStateChange(DROP, sensorValue2, upperBoundLow);
            }
        }
    }

    currentState = newState;
    sensorValue = sensorValue2;  //We'll use the current reading as the baseline next time around, so we can monitor the rate of change.
}









//Delay the overblow state until either it has timed out or the pressure has leveled off.
byte delayStateChange(byte jumpDrop, int pressure, int upper) {
    static unsigned long holdOffTimer;
    unsigned long now = millis();
    bool exitEarly = false;
    int rateChange;

    if (!holdoffActive) {  //Start our timer if we haven't already.
        holdoffActive = true;
        holdOffTimer = now;
        rateChangeIdx = 0;
        previousPressure = 0;
    }

    if ((jumpDrop == JUMP && (now - holdOffTimer) < jumpTime) || (jumpDrop == DROP && (now - holdOffTimer) < dropTime)) {  //Ff we haven't paused long enough, check the pressure rate change to see if it has leveled off.


        rateChange = pressureRateChange(pressure);

        if (rateChange != 2000) {  //make sure it's valid
            if (jumpDrop == JUMP && rateChange <= 0) {
                exitEarly = true;
            } else if (jumpDrop == DROP && rateChange >= 0) {
                exitEarly = true;
            }
        }
    }

    //if we've paused long enough or the pressure has stabilized, go to the bottom register
    else {
        holdoffActive = false;
        return BOTTOM_REGISTER;
    }

    if (exitEarly) {
        holdoffActive = false;
        return BOTTOM_REGISTER;
    }


    return currentState;  //stay in the current state if we haven't waited the total time and the pressure hasn't yet leveled off.
}








//Calculate the rate of pressure change to see if the slope has reversed
int pressureRateChange(int pressure) {
    int rateChange = 2000;  //if not valid
    if (rateChangeIdx == 0) {
        rateChangeIdx = 1;
        previousPressure = pressure;
    } else {
        rateChange = pressure - previousPressure;
        previousPressure = pressure;
    }


    return rateChange;
}










//calculate the upper boundary for the register when hysteresis is applied, from Louis Barman
int calcHysteresis(int currentUpperBound, bool high) {
    if (hysteresis == 0) {
        return currentUpperBound;
    }
    int range = currentUpperBound - sensorThreshold[0];
    int newUpperBound;
    if (high) {
        newUpperBound = currentUpperBound + (range * hysteresis) / 400;
    } else {
        newUpperBound = currentUpperBound - (range * hysteresis * 3) / 400;
    }

    return newUpperBound;
}








//calculate pitchbend expression based on pressure
void getExpression() {
    //calculate the center pressure value for the current note, regardless of register, unless "override" is turned on and we're not in overblow mode. In that case, use the override bounds instead


    int lowerBound;
    int useUpperBound;

    if (switches[mode][OVERRIDE] && (breathMode != kPressureBreath)) {
        lowerBound = (ED[mode][EXPRESSION_MIN] * 9) + 100;
        useUpperBound = (ED[mode][EXPRESSION_MAX] * 9) + 100;
    } else {
        lowerBound = sensorThreshold[0];
        if (newState == 3) {
            useUpperBound = upperBoundLow;  //get the register boundary taking hysteresis into consideration
        } else {
            useUpperBound = upperBoundHigh;
        }
    }

    unsigned int halfway = ((useUpperBound - lowerBound) >> 1) + lowerBound;  //calculate the midpoint of the curent register, where the note should play in tune.

    if (newState == 3) {
        halfway = useUpperBound + halfway;
        lowerBound = useUpperBound;
    }

    if (sensorValue < halfway) {
        byte scale = (((halfway - sensorValue) * ED[mode][EXPRESSION_DEPTH] * 20) / (halfway - lowerBound));  //should maybe figure out how to do this without dividing.
        expression = -((scale * scale) >> 3);
    } else {
        expression = (sensorValue - halfway) * ED[mode][EXPRESSION_DEPTH];
    }


    if (expression > ED[mode][EXPRESSION_DEPTH] * 200) {
        expression = ED[mode][EXPRESSION_DEPTH] * 200;  //put a cap on it, because in the upper register or in single-register mode, there's no upper limit
    }


    if (pitchBendMode == kPitchBendNone) {  //if we're not using vibrato, send the pitchbend now instead of adding it in later.
        pitchBend = 0;
        sendPitchbend();
    }
}








//find how many steps down to the next lower note on the scale.
void findStepsDown() {
    slideHole = findleftmostunsetbit(holeCovered);  //determine the highest uncovered hole, to use for sliding
    if (slideHole == 127) {                         //this means no holes are covered.
        // this could mean the highest hole is starting to be uncovered, so use that as the slideHole
        slideHole = 7;
        //return;
    }
    unsigned int closedSlideholePattern = holeCovered;
    bitSet(closedSlideholePattern, slideHole);                                    //figure out what the fingering pattern would be if we closed the slide hole
    stepsDown = constrain(tempNewNote - get_note(closedSlideholePattern), 0, 2);  //and then figure out how many steps down it would be if a new note were triggered with that pattern.
}








//Custom pitchbend algorithms, tin whistle and uilleann by Michael Eskin
void handleCustomPitchBend() {
    iPitchBend[2] = 0;  //reset pitchbend for the holes that are being used ToDo: may need to do this for all holes because others might be being used for slide.
    iPitchBend[3] = 0;

    if (pitchBendMode == kPitchBendSlideVibrato || pitchBendMode == kPitchBendLegatoSlideVibrato) {  //calculate slide if necessary.
        getSlide();
    }


    if (modeSelector[mode] != kModeGHB && modeSelector[mode] != kModeNorthumbrian) {  //only used for whistle and uilleann
        if (vibratoEnable == 1) {                                                     //if it's a vibrato fingering pattern
            if (slideHole != 2) {
                iPitchBend[2] = adjvibdepth;  //just assign max vibrato depth to a hole that isn't being used for sliding (it doesn't matter which hole, it's just so it will be added in later).
                iPitchBend[3] = 0;
            } else {
                iPitchBend[3] = adjvibdepth;
                iPitchBend[2] = 0;
            }
        }




        if (vibratoEnable == 0b000010) {  //used for whistle and uilleann, indicates that it's a pattern where lowering finger 2 or 3 partway would trigger progressive vibrato.

            if (modeSelector[mode] == kModeWhistle || modeSelector[mode] == kModeChromatic) {
                for (byte i = 2; i < 4; i++) {
                    if ((toneholeRead[i] > senseDistance) && (bitRead(holeCovered, i) != 1 && (i != slideHole))) {  //if the hole is contributing, bend down
                        iPitchBend[i] = ((toneholeRead[i] - senseDistance) * vibratoScale[i]) >> 3;
                    } else if (i != slideHole) {
                        iPitchBend[i] = 0;
                    }
                }
                if (iPitchBend[2] + iPitchBend[3] > adjvibdepth) {
                    iPitchBend[2] = adjvibdepth;  //cap at max vibrato depth if they combine to add up to more than that (just set one to max and the other to zero)
                    iPitchBend[3] = 0;
                }
            }


            else if (modeSelector[mode] == kModeUilleann || modeSelector[mode] == kModeUilleannStandard) {

                // If the back-D is open, and the vibrato hole completely open, max the pitch bend
                if ((holeCovered & 0b100000000) == 0) {
                    if (bitRead(holeCovered, 3) == 1) {
                        iPitchBend[3] = 0;
                    } else {
                        // Otherwise, bend down proportional to distance
                        if (toneholeRead[3] > senseDistance) {
                            iPitchBend[3] = adjvibdepth - (((toneholeRead[3] - senseDistance) * vibratoScale[3]) >> 3);
                        } else {
                            iPitchBend[3] = adjvibdepth;
                        }
                    }
                } else {

                    if ((toneholeRead[3] > senseDistance) && (bitRead(holeCovered, 3) != 1) && 3 != slideHole) {
                        iPitchBend[3] = ((toneholeRead[3] - senseDistance) * vibratoScale[3]) >> 3;
                    }

                    else if ((toneholeRead[3] < senseDistance) || (bitRead(holeCovered, 3) == 1)) {
                        iPitchBend[3] = 0;  // If the finger is removed or the hole is fully covered, there's no pitchbend contributed by that hole.
                    }
                }
            }
        }

    }


    else if (modeSelector[mode] == kModeGHB || modeSelector[mode] == kModeNorthumbrian) {  //this one is designed for closed fingering patterns, so raising a finger sharpens the note.
        for (byte i = 2; i < 4; i++) {                                                     //use holes 2 and 3 for vibrato
            if (i != slideHole || (holeCovered & 0b100000000) == 0) {
                static unsigned int testNote;                         // the hypothetical note that would be played if a finger were lowered all the way
                if (bitRead(holeCovered, i) != 1) {                   //if the hole is not fully covered
                    if (fingersChanged) {                             //if the fingering pattern has changed
                        testNote = get_note(bitSet(holeCovered, i));  //check to see what the new note would be
                        fingersChanged = 0;
                    }
                    if (testNote == newNote) {  //if the hole is uncovered and covering the hole wouldn't change the current note (or the left thumb hole is uncovered, because that case isn't included in the fingering chart)
                        if (toneholeRead[i] > senseDistance) {
                            iPitchBend[i] = 0 - (((toneholeCovered[i] - 50 - toneholeRead[i]) * vibratoScale[i]) >> 3);  //bend up, yielding a negative pitchbend value
                        } else {
                            iPitchBend[i] = 0 - adjvibdepth;  //if the hole is totally uncovered, max the pitchbend
                        }
                    }
                } else {                //if the hole is covered
                    iPitchBend[i] = 0;  //reset the pitchbend to 0
                }
            }
        }
        if ((((iPitchBend[2] + iPitchBend[3]) * -1) > adjvibdepth) && ((slideHole != 2 && slideHole != 3) || (holeCovered & 0b100000000) == 0)) {  //cap at vibrato depth if more than one hole is contributing and they add to up to more than the vibrato depth.
            iPitchBend[2] = 0 - adjvibdepth;                                                                                                       //assign max vibrato depth to a hole that isn't being used for sliding
            iPitchBend[3] = 0;
        }
    }
    sendPitchbend();
}









//Andrew's version of vibrato
void handlePitchBend() {
    for (byte i = 0; i < 9; i++) {  //reset
        iPitchBend[i] = 0;
    }

    if (pitchBendMode == kPitchBendSlideVibrato || pitchBendMode == kPitchBendLegatoSlideVibrato) {  //calculate slide if necessary.
        getSlide();
    }


    for (byte i = 0; i < 9; i++) {

        if (bitRead(holeLatched, i) == 1 && toneholeRead[i] < senseDistance) {
            (bitWrite(holeLatched, i, 0));  //we "unlatch" (enable for vibrato) a hole if it was covered when the note was triggered but now the finger has been completely removed.
        }

        //if this is a vibrato hole and we're in a mode that uses vibrato, and the hole is unlatched, and not the slide-hole and not the chromatic hole
        if (bitRead(vibratoHoles, i) == 1 && bitRead(holeLatched, i) == 0
            && (pitchBendMode == kPitchBendVibrato || (i != slideHole && !(modeSelector[mode] == kModeChromatic && i == 1)))) {
            if (toneholeRead[i] > senseDistance) {
                if (bitRead(holeCovered, i) != 1) {
                    iPitchBend[i] = (((toneholeRead[i] - senseDistance) * vibratoScale[i]) >> 3);  //bend downward
                    pitchBendOn[i] = 1;
                }
            } else {
                pitchBendOn[i] = 0;
                if (bitRead(holeCovered, i) == 1) {
                    iPitchBend[i] = 0;
                }
            }

            if (pitchBendOn[i] == 1 && (bitRead(holeCovered, i) == 1)) {
                iPitchBend[i] = adjvibdepth;  //set vibrato to max downward bend if a hole was being used to bend down and now is covered
            }
        }
    }


    sendPitchbend();
}








//calculate slide pitchBend, to be added with vibrato.
void getSlide() {
    for (byte i = 0; i < 9; i++) {
        if (toneholeRead[i] > senseDistance
            && ((i == slideHole && stepsDown > 0) || (i == 1 && modeSelector[mode] == kModeChromatic))) {
            if (bitRead(holeCovered, i) != 1) {
                if (i == 1 && modeSelector[mode] == kModeChromatic) {
                    iPitchBend[i] = ((toneholeRead[i] - senseDistance) * toneholeScale[i]) >> (3);  // bend down a half-step
                } else {
                    iPitchBend[i] = ((toneholeRead[i] - senseDistance) * toneholeScale[i]) >> (4 - stepsDown);  //bend down toward the next lowest note in the scale, the amount of bend depending on the number of steps down.
                }
            }
        } else {
            iPitchBend[i] = 0;
        }
    }
}









void sendPitchbend() {
    pitchBend = 0;  //reset the overall pitchbend in preparation for adding up the contributions from all the toneholes.
    for (byte i = 0; i < 9; i++) {
        pitchBend = pitchBend + iPitchBend[i];
    }

    int noteshift = 0;
    if (noteon && pitchBendModeSelector[mode] == kPitchBendLegatoSlideVibrato) {
        noteshift = (notePlaying - shift) - newNote;
        pitchBend += noteshift * pitchBendPerSemi;
    }

    pitchBend = 8192 - pitchBend + expression + shakeVibrato;

    if (pitchBend < 0) {
        pitchBend = 0;
    } else if (pitchBend > 16383) {
        pitchBend = 16383;
    }

    if (prevPitchBend != pitchBend) {

        if (noteon) {

            sendMIDI(PITCH_BEND, mainMidiChannel, pitchBend & 0x7F, pitchBend >> 7);
            prevPitchBend = pitchBend;
        }
    }
}








void calculateAndSendPitchbend() {
    if (ED[mode][EXPRESSION_ON] && !switches[mode][BAGLESS]) {
        getExpression();  //IF using pitchbend expression, calculate pitchbend based on pressure reading.
    }

    if (!customEnabled && pitchBendMode != kPitchBendNone) {
        handlePitchBend();
    } else if (customEnabled) {
        handleCustomPitchBend();
    }
}








//send MIDI NoteOn/NoteOff events when necessary
void sendNote() {
    const int velDelayMs = switches[mode][SEND_AFTERTOUCH] != 0 ? 3 : 16;  // keep this minimal to avoid latency if also sending aftertouch, but enough to get a good reading, otherwise use longer

    if (  //several conditions to tell if we need to turn on a new note.
      (!noteon
       || (pitchBendModeSelector[mode] != kPitchBendLegatoSlideVibrato && newNote != (notePlaying - shift))
       || (pitchBendModeSelector[mode] == kPitchBendLegatoSlideVibrato && abs(newNote - (notePlaying - shift)) > midiBendRange - 1))
      &&                                                                                    //if there wasn't any note playing or the current note is different than the previous one
      ((newState > 1 && !switches[mode][BAGLESS]) || (switches[mode][BAGLESS] && play)) &&  //and the state machine has determined that a note should be playing, or we're in bagless mode and the sound is turned on
      //!(prevNote == 62 && (newNote + shift) == 86) &&                                                   // and if we're currently on a middle D in state 3 (all finger holes covered), we wait until we get a new state reading before switching notes. This it to prevent erroneous octave jumps to a high D.
      !(switches[mode][SEND_VELOCITY] && !noteon && ((millis() - velocityDelayTimer) < velDelayMs)) &&  // and not waiting for the pressure to rise to calculate note on velocity if we're transitioning from not having any note playing.
      !(modeSelector[mode] == kModeNorthumbrian && newNote == 60) &&                                    //and if we're in Northumbrian mode don't play a note if all holes are covered. That simulates the closed pipe.
      !(breathMode != kPressureBell && bellSensor && holeCovered == 0b111111111)) {                     // don't play a note if the bell sensor and all other holes are covered, and we're not in "bell register" mode. Again, simulating a closed pipe.

        int notewason = noteon;
        int notewasplaying = notePlaying;


        // if this is a fresh/tongued note calculate pressure now to get the freshest initial velocity/pressure
        if (!notewason) {
            if (ED[mode][SEND_PRESSURE]) {
                calculatePressure(0);
            }
            if (switches[mode][SEND_VELOCITY]) {
                calculatePressure(1);
            }
            if (switches[mode][SEND_AFTERTOUCH] & 1) {
                calculatePressure(2);
            }
            if (switches[mode][SEND_AFTERTOUCH] & 2) {
                calculatePressure(3);
            }

            if (IMUsettings[mode][AUTOCENTER_YAW] == true && (nowtime - autoCenterYawTimer) > (IMUsettings[mode][AUTOCENTER_YAW_INTERVAL] * 250)) {  //Recenter yaw when we send a new note if there has been enough silence.
                centerIMU();
            }
        }


        if (notewason && !switches[mode][LEGATO]) {
            // send prior noteoff now if legato is selected.
            sendMIDI(NOTE_OFF, mainMidiChannel, notePlaying, 64);
            notewason = 0;
        }


        if (WARBL2settings[MIDI_DESTINATION] == 0 || connIntvl == 0) {                                                         //Only send here if not connected to BLE (to reduce jitter). I can't detect much difference, if any. (AM)
            if (ED[mode][SEND_PRESSURE] == 1 || switches[mode][SEND_AFTERTOUCH] != 0 || switches[mode][SEND_VELOCITY] == 1) {  // need to send pressure prior to note, in case we are using it for velocity
                sendPressure(true);
            }
        }

        // set it now so that send pitchbend will operate correctly
        noteon = 1;  //keep track of the fact that there's a note turned on
        notePlaying = newNote + shift;

        // send pitch bend immediately prior to note if necessary
        if (switches[mode][IMMEDIATE_PB]) {
            calculateAndSendPitchbend();
        }


        sendMIDI(NOTE_ON, mainMidiChannel, newNote + shift, velocity);  //send the new note

        if (notewason) {
            // turn off the previous note after turning on the new one (if it wasn't already done above)
            // We do it after to signal to synths that the notes are legato (not all synths will respond to this).
            sendMIDI(NOTE_OFF, mainMidiChannel, notewasplaying, 64);
        }

        pitchBendTimer = millis();  //for some reason it sounds best if we don't send pitchbend right away after starting a new note.
        noteOnTimestamp = pitchBendTimer;
        powerDownTimer = pitchBendTimer;  //reset the powerDown timer because we're actively sending notes

        prevNote = newNote;

        if (ED[mode][DRONES_CONTROL_MODE] == 2 && !dronesOn) {  //start drones if drones are being controlled with chanter on/off
            startDrones();
        }
    }


    if (noteon) {  //several conditions to turn a note off
        if (
          ((newState == 1 && !switches[mode][BAGLESS]) || (switches[mode][BAGLESS] && !play)) ||  //if the state drops to 1 (off) or we're in bagless mode and the sound has been turned off
          (modeSelector[mode] == kModeNorthumbrian && newNote == 60) ||                           //or closed Northumbrian pipe
          (breathMode != kPressureBell && bellSensor && holeCovered == 0b111111111)) {            //or completely closed pipe with any fingering chart
            sendMIDI(NOTE_OFF, mainMidiChannel, notePlaying, 64);                                 //turn the note off if the breath pressure drops or the bell sensor is covered and all the finger holes are covered.
                                                                                                  //keep track

            if (IMUsettings[mode][AUTOCENTER_YAW] == true) {  //Reset the autocenter yaw timer.
                autoCenterYawTimer = nowtime;
            }

            //Not sure if this is necessary here because it won't have an effect if there's no note playing(?) (AM)
            //sendPressure(true);

            noteon = 0;

            if (ED[mode][DRONES_CONTROL_MODE] == 2 && dronesOn) {  //stop drones if drones are being controlled with chanter on/off
                stopDrones();
            }
        }
    }
}










void blink()  //blink LED given number of times
{
    if (blinkNumber > 0) {
        if ((millis() - ledTimer) >= 200) {
            ledTimer = millis();

            if (LEDon) {
                digitalWrite(LEDpins[GREEN_LED], LOW);
                blinkNumber--;
                LEDon = 0;
                return;
            }

            else {
                digitalWrite(LEDpins[GREEN_LED], HIGH);
                LEDon = 1;
            }
        }
    }
}










void pulse()  //pulse LED
{
    static int writeValue[] = { 0, 0, 0 };
    static bool prevPulseState[] = { false, false, false };
    static int increment[] = { 1, 1, 1 };  //1 if currently increasing analog value, -1 if decreasing

    for (byte i = 0; i < 3; i++) {
        if (pulseLED[i] == true) {
            prevPulseState[i] = true;
            writeValue[i] += increment[i];  //Increment the analogWrite value.
            if (writeValue[i] == 1023) {
                increment[i] = -1;
            }
            if (writeValue[i] == 0) {
                increment[i] = 1;
            }
            analogWrite(LEDpins[i], writeValue[i]);

        } else if (prevPulseState[i] == true) {
            prevPulseState[i] = false;
            analogWrite(LEDpins[i], 0);  //If pulsing was just turned off, write 0.
            writeValue[i] = 0;           // //Reset so we'll start from 0 bext time pulse is turned on.
        }
    }
}










//check for and handle incoming MIDI messages from the WARBL Configuration Tool.
void handleControlChange(byte channel, byte number, byte value) {
    //Serial.println(channel);
    //Serial.println(number);
    //Serial.println(value);
    //Serial.println("");

    if (number < 120) {  //Chrome sends CC 121 and 123 on all channels when it connects, so ignore these.

        if ((channel & 0x0f) == 7) {   //If we're on channel 7, we may be receiving messages from the configuration tool.
            powerDownTimer = nowtime;  //Reset the powerDown timer because we've heard from the Config Tool.
            blinkNumber = 1;           //Blink once, indicating a received message. Some commands below will change this to three (or zero) blinks.


            ///////CC 102
            if (number == 102) {                 //Many settings are controlled by a value in CC 102 (always channel 7).
                if (value > 0 && value <= 18) {  //Handle sensor calibration commands from the configuration tool.
                    if ((value & 1) == 0) {
                        toneholeCovered[(value >> 1) - 1] -= 5;
                        if ((toneholeCovered[(value >> 1) - 1] - 54) < 5) {  //if the tonehole calibration gets set too low so that it would never register as being uncovered, send a message to the configuration tool.
                            sendMIDI(CC, 7, 102, (20 + ((value >> 1) - 1)));
                        }
                    } else {
                        toneholeCovered[((value + 1) >> 1) - 1] += 5;
                    }
                }

                if (value == 19) {  //Save calibration if directed.
                    saveCalibration();
                    blinkNumber = 3;
                }

                else if (value == 127) {  //Begin auto-calibration if directed.
                    blinkNumber = 0;
                    calibration = 1;
                }

                else if (value == 126) {  //When communication is established, send all current settings to tool.
                    communicationMode = 1;
                    sendSettings();
                }

                else if (value == 99) {  //Turn off communication mode.
                    communicationMode = 0;
                }


                for (byte i = 0; i < 3; i++) {  // Update the three selected fingering patterns if prompted by the tool.
                    if (value == 30 + i) {
                        fingeringReceiveMode = i;
                    }
                }

                if ((value > 32 && value < 60) || (value > 99 && value < 104)) {
                    modeSelector[fingeringReceiveMode] = value - 33;
                    loadPrefs();
                }


                for (byte i = 0; i < 3; i++) {  //Update current mode (instrument) if directed.
                    if (value == 60 + i) {
                        mode = i;
                        play = 0;
                        loadPrefs();     //Load the correct user settings based on current instrument.
                        sendSettings();  //Send settings for new mode to tool.
                        blinkNumber = abs(mode) + 1;
                    }
                }

                for (byte i = 0; i < 4; i++) {  //Update current pitchbend mode if directed.
                    if (value == 70 + i) {
                        pitchBendModeSelector[mode] = i;
                        loadPrefs();
                        blinkNumber = abs(pitchBendMode) + 1;
                    }
                }

                for (byte i = 0; i < 5; i++) {  //Update current breath mode if directed.
                    if (value == 80 + i) {
                        breathModeSelector[mode] = i;
                        loadPrefs();  //Load the correct user settings based on current instrument.
                        blinkNumber = abs(breathMode) + 1;
                    }
                }

                for (byte i = 0; i < 8; i++) {  //Update button receive mode (this indicates the row in the button settings for which the next received byte will be).
                    if (value == 90 + i) {
                        buttonReceiveMode = i;
                        blinkNumber = 0;
                    }
                }

                for (byte i = 0; i < 8; i++) {  //update button configuration
                    if (buttonReceiveMode == i) {

                        for (byte k = 0; k < 5; k++) {  //update column 1 (MIDI action).
                            if (value == 112 + k) {
                                buttonPrefs[mode][i][1] = k;
                            }
                        }
                    }
                }

                for (byte i = 0; i < 3; i++) {  //update momentary
                    if (buttonReceiveMode == i) {
                        if (value == 117) {
                            momentary[mode][i] = 0;
                            noteOnOffToggle[i] = 0;
                        } else if (value == 118) {
                            momentary[mode][i] = 1;
                            noteOnOffToggle[i] = 0;
                        }
                    }
                }

                if (value == 85) {  //set current Instrument as default and save default to settings.
                    defaultMode = mode;
                    writeEEPROM(48, defaultMode);
                }


                if (value == 123) {  //save settings as the defaults for the current instrument
                    saveSettings(mode);
                    blinkNumber = 3;
                }


                else if (value == 124) {  //Save settings as the defaults for all instruments
                    for (byte k = 0; k < 3; k++) {
                        saveSettings(k);
                    }
                    loadFingering();
                    loadSettingsForAllModes();
                    loadPrefs();
                    blinkNumber = 3;

                }

                else if (value == 125) {  //restore all factory settings
                    restoreFactorySettings();
                    blinkNumber = 3;
                }
            }




            ///////CC 103
            else if (number == 103) {
                senseDistanceSelector[mode] = value;
                loadPrefs();
            }

            else if (number == 117) {
                unsigned long v = value * 8191UL / 100;
                vibratoDepthSelector[mode] = v;  //scale vibrato depth in cents up to pitchbend range of 0-8191
                loadPrefs();
            }


            for (byte i = 0; i < 3; i++) {  //update noteshift
                if (number == 111 + i) {
                    if (value < 50) {
                        noteShiftSelector[i] = value;
                    } else {
                        noteShiftSelector[i] = -127 + value;
                    }
                    loadPrefs();
                }
            }



            ///////CC 104
            if (number == 104) {  //update receive mode, used for advanced pressure range sliders, switches, and expression and drones panel settings (this indicates the variable for which the next received byte on CC 105 will be).
                pressureReceiveMode = value - 1;
            }



            ///////CC 105
            else if (number == 105) {

                if (pressureReceiveMode < 12) {
                    pressureSelector[mode][pressureReceiveMode] = value;  //advanced pressure values
                    loadPrefs();
                }

                else if (pressureReceiveMode < 33) {
                    ED[mode][pressureReceiveMode - 12] = value;  //expression and drones settings
                    loadPrefs();
                }

                else if (pressureReceiveMode == 33) {
                    LSBlearnedPressure = value;

                }

                else if (pressureReceiveMode == 34) {
                    learnedPressureSelector[mode] = (value << 7) | LSBlearnedPressure;
                    loadPrefs();
                }


                else if (pressureReceiveMode < 53) {
                    switches[mode][pressureReceiveMode - 39] = value;  //switches in the slide/vibrato and register control panels
                    loadPrefs();
                }

                else if (pressureReceiveMode == 60) {
                    midiBendRangeSelector[mode] = value;
                    loadPrefs();
                }

                else if (pressureReceiveMode == 61) {
                    midiChannelSelector[mode] = value;
                    loadPrefs();
                }

                else if (pressureReceiveMode < 98) {
                    ED[mode][pressureReceiveMode - 48] = value;  //more expression and drones settings
                    loadPrefs();
                }

                else if (pressureReceiveMode >= 200 && pressureReceiveMode < (kIMUnVariables + 200)) {
                    IMUsettings[mode][pressureReceiveMode - 200] = value;  //IMU settings
                    loadPrefs();
                }

                else if (pressureReceiveMode >= 300 && pressureReceiveMode < 304) {  //WARBL2 Custom fingering charts
                    blinkNumber = 0;
                    WARBL2CustomChart[WARBL2CustomChartReceiveByte] = value;  //Put the value in the custom chart.
                    WARBL2CustomChartReceiveByte++;                           //Increment the location

                    if (WARBL2CustomChartReceiveByte == 256) {
                        WARBL2CustomChartReceiveByte = 0;  //Reset for next time;
                        for (int i = 0; i < 256; i++) {    //Write the chart to EEPROM.
                            writeEEPROM((3000 + (256 * (pressureReceiveMode - 300))) + i, WARBL2CustomChart[i]);
                        }
                        blinkNumber = 3;
                        sendMIDI(CC, 7, 109, 100);  //Indicate success.
                        loadPrefs();
                    }
                }
            }



            ///////CC 109
            if ((number == 109 && value < kIMUnVariables) || (number == 109 && value > 99 && value < 104)) {  //Indicates that value for IMUsettings variable will be sent on CC 105.
                pressureReceiveMode = value + 200;                                                            //Add to the value because lower pressureReceiveModes are used for other variables.
                blinkNumber = 0;
            }


            ///////CC 106
            if (number == 106 && value > 15) {


                if (value > 19 && value < 29) {  //update enabled vibrato holes for "universal" vibrato
                    bitSet(vibratoHolesSelector[mode], value - 20);
                    loadPrefs();
                }

                else if (value > 29 && value < 39) {
                    bitClear(vibratoHolesSelector[mode], value - 30);
                    loadPrefs();
                }

                else if (value == 39) {
                    useLearnedPressureSelector[mode] = 0;
                    loadPrefs();
                }

                else if (value == 40) {
                    useLearnedPressureSelector[mode] = 1;
                    loadPrefs();
                }

                else if (value == 41) {
                    learnedPressureSelector[mode] = sensorValue;
                    sendMIDI(CC, 7, 104, 34);                                    //indicate that LSB of learned pressure is about to be sent
                    sendMIDI(CC, 7, 105, learnedPressureSelector[mode] & 0x7F);  //send LSB of learned pressure
                    sendMIDI(CC, 7, 104, 35);                                    //indicate that MSB of learned pressure is about to be sent
                    sendMIDI(CC, 7, 105, learnedPressureSelector[mode] >> 7);    //send MSB of learned pressure
                    loadPrefs();
                }

                else if (value == 42) {  //autocalibrate bell sensor only
                    calibration = 2;
                    blinkNumber = 0;
                }


                else if (value == 43) {
                    int tempPressure = sensorValue;
                    ED[mode][DRONES_PRESSURE_LOW_BYTE] = tempPressure & 0x7F;
                    ED[mode][DRONES_PRESSURE_HIGH_BYTE] = tempPressure >> 7;
                    sendMIDI(CC, 7, 104, 32);                                   //indicate that LSB of learned drones pressure is about to be sent
                    sendMIDI(CC, 7, 105, ED[mode][DRONES_PRESSURE_LOW_BYTE]);   //send LSB of learned drones pressure
                    sendMIDI(CC, 7, 104, 33);                                   //indicate that MSB of learned drones pressure is about to be sent
                    sendMIDI(CC, 7, 105, ED[mode][DRONES_PRESSURE_HIGH_BYTE]);  //send MSB of learned drones pressure
                }


                else if (value == 45) {  //save current sensor calibration as factory calibration
                    for (byte i = 18; i < 38; i++) {
                        writeEEPROM(i + 1500, readEEPROM(i));
                    }
                    for (int i = 1; i < 10; i++) {  //save baseline calibration as factory baseline
                        writeEEPROM(i + 1500, readEEPROM(i));
                    }
                    for (int i = 985; i < 997; i++) {  //save gyroscope calibration as factory calibration
                        writeEEPROM(i + 1500, readEEPROM(i));
                    }
                }

                else if (value == 54) {
                    calibrateIMU();
                }

                else if (value > 54 && value < (55 + kWARBL2SETTINGSnVariables)) {
                    WARBL2settingsReceiveMode = value - 55;
                }

                else if (value == 60) {  // recenter IMU heading based on current
                    centerIMU();
                }

                else if (value > 99) {
                    for (byte i = 0; i < 8; i++) {  //update button configuration
                        if (buttonReceiveMode == i) {

                            for (byte j = 0; j < 27; j++) {  //update column 0 (action).
                                if (value == 100 + j) {
                                    buttonPrefs[mode][i][0] = j;
                                }
                            }
                        }
                    }
                }
            }




            ///////CC 119
            if (number == 119) {
                WARBL2settings[WARBL2settingsReceiveMode] = value;
                for (byte r = 0; r < kWARBL2SETTINGSnVariables; r++) {  //save the WARBL2settings array each time it is changed by the Config Tool because it is independent of mode.
                    writeEEPROM(600 + r, WARBL2settings[r]);
                }
            }



            for (byte i = 0; i < 8; i++) {  //update channel, byte 2, byte 3 for MIDI message for button MIDI command for row i
                if (buttonReceiveMode == i) {
                    if (number == 106 && value < 16) {
                        buttonPrefs[mode][i][2] = value;
                    } else if (number == 107) {
                        buttonPrefs[mode][i][3] = value;
                    } else if (number == 108) {
                        buttonPrefs[mode][i][4] = value;
                    }
                }
            }
        }
    }  //end of ignore CCs 121, 123
}









//interpret button presses. If the button is being used for momentary MIDI messages we ignore other actions with that button (except "secret" actions involving the toneholes).
void handleButtons() {
    //first, some housekeeping

    if (shiftState == 1 && released[1] == 1) {  //if button 1 was only being used along with another button, we clear the just-released flag for button 1 so it doesn't trigger another control change.
        released[1] = 0;
        buttonUsed = 0;  //clear the button activity flag, so we won't handle them again until there's been new button activity.
        shiftState = 0;
    }


    //then, a few hard-coded actions that can't be changed by the configuration tool:
    //_______________________________________________________________________________

    if (justPressed[0] && !pressed[2] && !pressed[1]) {
        if (ED[mode][DRONES_CONTROL_MODE] == 1) {
            if (holeCovered >> 1 == 0b00001000) {  //turn drones on/off if button 0 is pressed and fingering pattern is 0 0001000.
                justPressed[0] = 0;
                specialPressUsed[0] = 1;
                if (!dronesOn) {
                    startDrones();
                } else {
                    stopDrones();
                }
            }
        }

        if (switches[mode][SECRET]) {
            if (holeCovered >> 1 == 0b00010000) {  //change pitchbend mode if button 0 is pressed and fingering pattern is 0 0000010.
                justPressed[0] = 0;
                specialPressUsed[0] = 1;
                changePitchBend();
            }

            else if (holeCovered >> 1 == 0b00000010) {  //change instrument if button 0 is pressed and fingering pattern is 0 0000001.
                justPressed[0] = 0;
                specialPressUsed[0] = 1;
                changeInstrument();
            }
        }
    }


    //now the button actions that can be changed with the configuration tool.
    //_______________________________________________________________________________


    for (byte i = 0; i < 3; i++) {


        if (released[i] && (momentary[mode][i] || (pressed[0] + pressed[1] + pressed[2] == 0))) {  //do action for a button release ("click") NOTE: button array is zero-indexed, so "button 1" in all documentation is button 0 here (same for others).
            if (!specialPressUsed[i]) {                                                            //we ignore it if the button was just used for a hard-coded command involving a combination of fingerholes.
                performAction(i);
            }
            released[i] = 0;
            specialPressUsed[i] = 0;
        }


        if (longPress[i] && (pressed[0] + pressed[1] + pressed[2] == 1) && !momentary[mode][i]) {  //do action for long press, assuming no other button is pressed.

            performAction(5 + i);
            longPressUsed[i] = 1;
            longPress[i] = 0;
            longPressCounter[i] = 0;
        }


        //presses of individual buttons (as opposed to releases) are special cases used only if we're using buttons to send MIDI on/off messages and "momentary" is selected. We'll handle these in a separate function.
        if (justPressed[i]) {
            justPressed[i] = 0;
            handleMomentary(i);  //do action for button press.
        }
    }


    if (pressed[1]) {
        if (released[0] && !momentary[mode][0]) {  //do action for button 1 held and button 0 released
            released[0] = 0;
            shiftState = 1;
            performAction(3);
        }

        if (released[2] && !momentary[mode][1]) {  //do action for button 1 held and button 2 released
            released[2] = 0;
            shiftState = 1;
            performAction(4);
        }
    }


    buttonUsed = 0;  // Now that we've caught any important button activity, clear the flag so we won't enter this function again until there's been new activity.
}







//perform desired action in response to buttons
void performAction(byte action) {

    //Serial.println((buttonPrefs[mode][action][0]));

    switch (buttonPrefs[mode][action][0]) {

        case 0:
            break;  //if no action desired for button combination

        case 1:  //MIDI command

            if (buttonPrefs[mode][action][1] == 0) {
                if (noteOnOffToggle[action] == 0) {
                    sendMIDI(NOTE_ON, buttonPrefs[mode][action][2], buttonPrefs[mode][action][3], buttonPrefs[mode][action][4]);
                    noteOnOffToggle[action] = 1;
                } else if (noteOnOffToggle[action] == 1) {
                    sendMIDI(NOTE_OFF, buttonPrefs[mode][action][2], buttonPrefs[mode][action][3], buttonPrefs[mode][action][4]);
                    noteOnOffToggle[action] = 0;
                }
            }

            if (buttonPrefs[mode][action][1] == 1) {
                sendMIDI(CC, buttonPrefs[mode][action][2], buttonPrefs[mode][action][3], buttonPrefs[mode][action][4]);
            }

            if (buttonPrefs[mode][action][1] == 2) {
                sendMIDI(PROGRAM_CHANGE, buttonPrefs[mode][action][2], buttonPrefs[mode][action][3]);
            }

            if (buttonPrefs[mode][action][1] == 3) {  //increase program change
                if (program < 127) {
                    program++;
                } else {
                    program = 0;
                }
                sendMIDI(PROGRAM_CHANGE, buttonPrefs[mode][action][2], program);
                blinkNumber = 1;
            }

            if (buttonPrefs[mode][action][1] == 4) {  //decrease program change
                if (program > 0) {
                    program--;
                } else {
                    program = 127;
                }
                sendMIDI(PROGRAM_CHANGE, buttonPrefs[mode][action][2], program);
                blinkNumber = 1;
            }

            break;

        case 2:  //set vibrato/slide mode
            changePitchBend();
            break;

        case 3:
            changeInstrument();
            break;

        case 4:
            play = !play;  //turn sound on/off when in bagless mode
            break;

        case 5:
            if (!momentary[mode][action]) {  //shift up unless we're in momentary mode, otherwise shift down
                octaveShiftUp();
                blinkNumber = abs(octaveShift);
            } else {
                octaveShiftDown();
            }
            break;

        case 6:
            if (!momentary[mode][action]) {  //shift down unless we're in momentary mode, otherwise shift up
                octaveShiftDown();
                blinkNumber = abs(octaveShift);
            } else {
                octaveShiftUp();
            }
            break;

        case 7:
            for (byte i = 1; i < 17; i++) {  //send MIDI panic
                sendMIDI(CC, i, 123, 0);
                dronesOn = 0;  //remember that drones are off, because MIDI panic will have most likely turned them off in all apps.
            }
            break;

        case 8:
            breathModeSelector[mode]++;  //set breath mode
            if (breathModeSelector[mode] == kPressureNModes) {
                breathModeSelector[mode] = kPressureSingle;
            }
            loadPrefs();
            play = 0;
            blinkNumber = abs(breathMode) + 1;
            if (communicationMode) {
                sendMIDI(CC, 7, 102, 80 + breathMode);  //send current breathMode
            }
            break;


        case 9:  //toggle drones
            blinkNumber = 1;
            if (!dronesOn) {
                startDrones();
            } else {
                stopDrones();
            }
            break;


        case 10:  //semitone shift up
            if (!momentary[mode][action]) {
                noteShift++;  //shift up if we're not in momentary mode
            } else {
                noteShift--;  //shift down if we're in momentary mode, because the button is being released and a previous press has shifted up.
            }
            break;


        case 11:  //semitone shift down
            if (!momentary[mode][action]) {
                noteShift--;  //shift up if we're not in momentary mode
            } else {
                noteShift++;  //shift down if we're in momentary mode, because the button is being released and a previous press has shifted up.
            }
            break;


        case 12:  //autocalibrate
            calibration = 1;
            break;


        case 13:  //Power down
            powerDown(false);
            break;


        case 14:  //Recenter yaw
            centerIMU();
            break;



        default:
            return;
    }
}






void octaveShiftUp() {
    if (octaveShift < 3) {
        octaveShiftSelector[mode]++;  //adjust octave shift up, within reason
        octaveShift = octaveShiftSelector[mode];
    }
}






void octaveShiftDown() {
    if (octaveShift > -4) {
        octaveShiftSelector[mode]--;
        octaveShift = octaveShiftSelector[mode];
    }
}





//cycle through pitchbend modes
void changePitchBend() {
    pitchBendModeSelector[mode]++;
    if (pitchBendModeSelector[mode] == kPitchBendNModes) {
        pitchBendModeSelector[mode] = kPitchBendSlideVibrato;
    }
    loadPrefs();
    blinkNumber = abs(pitchBendMode) + 1;
    if (communicationMode) {
        sendMIDI(CC, 7, 102, 70 + pitchBendMode);  //send current pitchbend mode to configuration tool.
    }
}






//cycle through instruments
void changeInstrument() {
    mode++;  //set instrument
    if (mode == 3) {
        mode = 0;
    }
    play = 0;
    loadPrefs();  //load the correct user settings based on current instrument.
    blinkNumber = abs(mode) + 1;
    if (communicationMode) {
        sendSettings();  //tell communications tool to switch mode and send all settings for current instrument.
    }
}






void handleMomentary(byte button) {
    if (momentary[mode][button]) {
        if (buttonPrefs[mode][button][0] == 1 && buttonPrefs[mode][button][1] == 0) {  //handle momentary press if we're sending a MIDI message
            sendMIDI(NOTE_ON, buttonPrefs[mode][button][2], buttonPrefs[mode][button][3], buttonPrefs[mode][button][4]);
            noteOnOffToggle[button] = 1;
        }

        //handle presses for shifting the octave or semitone up or down
        if (buttonPrefs[mode][button][0] == 5) {
            octaveShiftUp();
        }

        if (buttonPrefs[mode][button][0] == 6) {
            octaveShiftDown();
        }

        if (buttonPrefs[mode][button][0] == 10) {
            noteShift++;
        }

        if (buttonPrefs[mode][button][0] == 11) {
            noteShift--;
        }
    }
}








void startDrones() {
    dronesOn = 1;
    switch (ED[mode][DRONES_ON_COMMAND]) {
        case 0:
            sendMIDI(NOTE_ON, ED[mode][DRONES_ON_CHANNEL], ED[mode][DRONES_ON_BYTE2], ED[mode][DRONES_ON_BYTE3]);
            break;
        case 1:
            sendMIDI(NOTE_OFF, ED[mode][DRONES_ON_CHANNEL], ED[mode][DRONES_ON_BYTE2], ED[mode][DRONES_ON_BYTE3]);
            break;
        case 2:
            sendMIDI(CC, ED[mode][DRONES_ON_CHANNEL], ED[mode][DRONES_ON_BYTE2], ED[mode][DRONES_ON_BYTE3]);
            break;
    }
}








void stopDrones() {
    dronesOn = 0;
    switch (ED[mode][DRONES_OFF_COMMAND]) {
        case 0:
            sendMIDI(NOTE_ON, ED[mode][DRONES_OFF_CHANNEL], ED[mode][DRONES_OFF_BYTE2], ED[mode][DRONES_OFF_BYTE3]);
            break;
        case 1:
            sendMIDI(NOTE_OFF, ED[mode][DRONES_OFF_CHANNEL], ED[mode][DRONES_OFF_BYTE2], ED[mode][DRONES_OFF_BYTE3]);
            break;
        case 2:
            sendMIDI(CC, ED[mode][DRONES_OFF_CHANNEL], ED[mode][DRONES_OFF_BYTE2], ED[mode][DRONES_OFF_BYTE3]);
            break;
    }
}








// find leftmost unset bit, used for finding the uppermost uncovered hole when reading from some fingering charts, and for determining the slidehole.
byte findleftmostunsetbit(uint16_t n) {
    if ((n & (n + 1)) == 0) {
        return 127;  // if number contains all 0s then return 127
    }

    int pos = 0;
    for (int temp = n, count = 0; temp > 0; temp >>= 1, count++)  // Find position of leftmost unset bit.

        if ((temp & 1) == 0)  // if temp L.S.B is zero then unset bit pos is
            pos = count;

    return pos;
}









//This is used the first time the software is run, to copy all the default settings to EEPROM.
void saveFactorySettings() {
    for (byte i = 0; i < 3; i++) {  //save all the current settings for all three instruments.
        mode = i;
        saveSettings(i);
    }

    writeEEPROM(48, defaultMode);  //save default mode

    for (byte r = 0; r < kWARBL2SETTINGSnVariables; r++) {  //save the WARBL2settings array
        writeEEPROM(600 + r, WARBL2settings[r]);
    }

    writeEEPROM(1009, highByte(fullRunTime));  //The initial estimate of the total run time available on a full charge (minutes)
    writeEEPROM(1010, lowByte(fullRunTime));

    writeEEPROM(1013, highByte(prevRunTime));  //The elapsed run time on the currrent charge (minutes)--from the "factory" we set this to an arbitrary number of minutes because the battery charge state is unknown.
    writeEEPROM(1014, lowByte(prevRunTime));

    writeEEPROM(44, 3);  //indicates settings have been saved

    for (int i = 39; i < 980; i++) {           //then we read every byte in EEPROM from 39 to 999 (Doesn't include sensor calibrations because we don't want to overwrite factory calibrations)
        writeEEPROM(1500 + i, readEEPROM(i));  //and rewrite them from 1501 to 2499. Then they'll be available to restore later if necessary.
    }

    blinkNumber = 3;
}








//restore original settings from EEPROM
void restoreFactorySettings() {
    for (int i = 1; i < 1000; i++) {  //Read factory settings and rewrite to the normal settings locations.
        writeEEPROM(i, readEEPROM(1500 + i));
    }

    //load the newly restored settings
    loadFingering();
    loadCalibration();
    loadSettingsForAllModes();
    loadPrefs();
    communicationMode = 1;  //We are connected to the Config Tool because that's what initiated restoring settings.
    sendSettings();         //Send the new settings.
}








//Save settings for current instrument as defaults for given instrument (i).
void saveSettings(byte i) {
    writeEEPROM(40 + i, modeSelector[mode]);
    writeEEPROM(53 + i, noteShiftSelector[mode]);
    writeEEPROM(50 + i, senseDistanceSelector[mode]);

    for (byte n = 0; n < kSWITCHESnVariables; n++) {  //Saved in this format, we can add more variables to the arrays without overwriting the existing EEPROM locations.
        writeEEPROM((56 + n + (i * kSWITCHESnVariables)), switches[mode][n]);
    }

    writeEEPROM(333 + (i * 2), lowByte(vibratoHolesSelector[mode]));
    writeEEPROM(334 + (i * 2), highByte(vibratoHolesSelector[mode]));
    writeEEPROM(339 + (i * 2), lowByte(vibratoDepthSelector[mode]));
    writeEEPROM(340 + (i * 2), highByte(vibratoDepthSelector[mode]));
    writeEEPROM(345 + i, useLearnedPressureSelector[mode]);

    for (byte j = 0; j < 5; j++) {  //save button configuration for current mode
        for (byte k = 0; k < 8; k++) {
            writeEEPROM(100 + (i * 50) + (j * 10) + k, buttonPrefs[mode][k][j]);
        }
    }

    for (byte h = 0; h < 3; h++) {
        writeEEPROM(250 + (i * 3) + h, momentary[mode][h]);
    }

    for (byte q = 0; q < 12; q++) {
        writeEEPROM((260 + q + (i * 20)), pressureSelector[mode][q]);
    }

    writeEEPROM(273 + (i * 2), lowByte(learnedPressureSelector[mode]));
    writeEEPROM(274 + (i * 2), highByte(learnedPressureSelector[mode]));

    writeEEPROM(313 + i, pitchBendModeSelector[mode]);
    writeEEPROM(316 + i, breathModeSelector[mode]);
    writeEEPROM(319 + i, midiBendRangeSelector[mode]);
    writeEEPROM(322 + i, midiChannelSelector[mode]);

    for (byte n = 0; n < kEXPRESSIONnVariables; n++) {
        writeEEPROM((351 + n + (i * kEXPRESSIONnVariables)), ED[mode][n]);
    }

    for (byte n = 0; n < kIMUnVariables; n++) {
        writeEEPROM((625 + n + (i * kIMUnVariables)), IMUsettings[mode][n]);
    }
}









//Send all settings for current instrument to the WARBL Configuration Tool. New variables should be added at the end to maintain backweard compatability with settings import/export in the Config Tool.
void sendSettings() {

    sendMIDI(CC, 7, 110, VERSION);  //Send the firmware version.

    for (byte i = 0; i < 3; i++) {
        sendMIDI(CC, 7, 102, 30 + i);                //Indicate that we'll be sending the fingering pattern for instrument i.
        sendMIDI(CC, 7, 102, 33 + modeSelector[i]);  //Send

        if (noteShiftSelector[i] >= 0) {
            sendMIDI(CC, 7, 111 + i, noteShiftSelector[i]);
        }  //Send noteShift, with a transformation for sending negative values over MIDI.
        else {
            sendMIDI(CC, 7, 111 + i, noteShiftSelector[i] + 127);
        }
    }

    sendMIDI(CC, 7, 102, 60 + mode);         //Send current instrument.
    sendMIDI(CC, 7, 102, 85 + defaultMode);  //Send default instrument.

    sendMIDI(CC, 7, 103, senseDistance);  //Send sense distance

    sendMIDI(CC, 7, 117, vibratoDepth * 100UL / 8191);  //Send vibrato depth, scaled down to cents.
    sendMIDI(CC, 7, 102, 70 + pitchBendMode);           //Send current pitchBend mode.
    sendMIDI(CC, 7, 102, 80 + breathMode);              //Send current breathMode.
    sendMIDI(CC, 7, 102, 120 + bellSensor);             //Send bell sensor state.
    sendMIDI(CC, 7, 106, 39 + useLearnedPressure);      //Send calibration option.
    sendMIDI(CC, 7, 104, 34);                           //Indicate that LSB of learned pressure is about to be sent.
    sendMIDI(CC, 7, 105, learnedPressure & 0x7F);       //Send LSB of learned pressure.
    sendMIDI(CC, 7, 104, 35);                           //Indicate that MSB of learned pressure is about to be sent.
    sendMIDI(CC, 7, 105, learnedPressure >> 7);         //Send MSB of learned pressure.

    sendMIDI(CC, 7, 104, 61);             // Indicate midi bend range is about to be sent.
    sendMIDI(CC, 7, 105, midiBendRange);  //MIDI bend range

    sendMIDI(CC, 7, 104, 62);               // Indicate MIDI channel is about to be sent.
    sendMIDI(CC, 7, 105, mainMidiChannel);  //Midi bend range



    for (byte i = 0; i < 9; i++) {
        sendMIDI(CC, 7, 106, 20 + i + (10 * (bitRead(vibratoHolesSelector[mode], i))));  //Send enabled vibrato holes.
    }

    for (byte i = 0; i < 8; i++) {
        sendMIDI(CC, 7, 102, 90 + i);                         //Indicate that we'll be sending data for button commands row i (click 1, click 2, etc.).
        sendMIDI(CC, 7, 106, 100 + buttonPrefs[mode][i][0]);  //Send action (i.e. none, send MIDI message, etc.).
        if (buttonPrefs[mode][i][0] == 1) {                   //If the action is a MIDI command, send the rest of the MIDI info for that row.
            sendMIDI(CC, 7, 102, 112 + buttonPrefs[mode][i][1]);
            sendMIDI(CC, 7, 106, buttonPrefs[mode][i][2]);
            sendMIDI(CC, 7, 107, buttonPrefs[mode][i][3]);
            sendMIDI(CC, 7, 108, buttonPrefs[mode][i][4]);
        }
    }

    for (byte i = 0; i < kSWITCHESnVariables; i++) {  //Send settings for switches in the slide/vibrato and register control panels.
        sendMIDI(CC, 7, 104, i + 40);
        sendMIDI(CC, 7, 105, switches[mode][i]);
    }

    for (byte i = 0; i < 21; i++) {  //Send settings for expression and drones control panels.
        sendMIDI(CC, 7, 104, i + 13);
        sendMIDI(CC, 7, 105, ED[mode][i]);
    }

    for (byte i = 21; i < 49; i++) {  //More settings for expression and drones control panels.
        sendMIDI(CC, 7, 104, i + 49);
        sendMIDI(CC, 7, 105, ED[mode][i]);
    }

    for (byte i = 0; i < 3; i++) {
        sendMIDI(CC, 7, 102, 90 + i);  //Indicate that we'll be sending data for momentary.
        sendMIDI(CC, 7, 102, 117 + momentary[mode][i]);
    }

    for (byte i = 0; i < 12; i++) {
        sendMIDI(CC, 7, 104, i + 1);                      //Indicate which pressure variable we'll be sending.
        sendMIDI(CC, 7, 105, pressureSelector[mode][i]);  //Send the data.
    }

    sendMIDI(CC, 7, 102, 121);  //Tell the Config Tool that the bell sensor is present (always on this version of the WARBL).

    for (byte i = 55; i < 55 + kWARBL2SETTINGSnVariables; i++) {  //Send the WARBL2settings array.
        sendMIDI(CC, 7, 106, i);
        sendMIDI(CC, 7, 119, WARBL2settings[i - 55]);
    }

    manageBattery(true);  //Do this to send voltage and charging status to Config Tool.

    sendMIDI(CC, 7, 106, 72);
    sendMIDI(CC, 7, 119, (connIntvl * 100) & 0x7F);  //Send low byte of the connection interval.
    sendMIDI(CC, 7, 106, 73);
    sendMIDI(CC, 7, 119, (connIntvl * 100) >> 7);  //Send high byte of the connection interval.

    for (byte i = 0; i < kIMUnVariables; i++) {  //IMU settings
        sendMIDI(CC, 7, 109, i);
        sendMIDI(CC, 7, 105, IMUsettings[mode][i]);
    }
}










//load saved fingering patterns
void loadFingering() {
    for (byte i = 0; i < 3; i++) {
        modeSelector[i] = readEEPROM(40 + i);
        noteShiftSelector[i] = (int8_t)readEEPROM(53 + i);

        if (communicationMode) {
            sendMIDI(CC, 7, 102, 30 + i);                //indicate that we'll be sending the fingering pattern for instrument i
            sendMIDI(CC, 7, 102, 33 + modeSelector[i]);  //send

            if (noteShiftSelector[i] >= 0) {
                sendMIDI(CC, 7, (111 + i), noteShiftSelector[i]);
            }  //send noteShift, with a transformation for sending negative values over MIDI.
            else {
                sendMIDI(CC, 7, (111 + i), noteShiftSelector[i] + 127);
            }
        }
    }
}







//load settings for all three instruments from EEPROM
void loadSettingsForAllModes() {

    //Some things that are independent of mode.
    defaultMode = readEEPROM(48);  //load default mode

    for (byte r = 0; r < kWARBL2SETTINGSnVariables; r++) {  //Load the WARBL2settings array.
        WARBL2settings[r] = readEEPROM(600 + r);
    }

    fullRunTime = (word(readEEPROM(1009), readEEPROM(1010)));  //The total run time available on a full charge (minutes)
    prevRunTime = (word(readEEPROM(1013), readEEPROM(1014)));  //The total run time since the last full charge (minutes)

    getEEPROM(985, gyroXCalibration);
    getEEPROM(989, gyroYCalibration);
    getEEPROM(993, gyroZCalibration);


    //Do all this for each mode.
    for (byte i = 0; i < 3; i++) {
        senseDistanceSelector[i] = readEEPROM(50 + i);

        for (byte n = 0; n < kSWITCHESnVariables; n++) {
            switches[i][n] = readEEPROM(56 + n + (i * kSWITCHESnVariables));
        }

        vibratoHolesSelector[i] = word(readEEPROM(334 + (i * 2)), readEEPROM(333 + (i * 2)));
        vibratoDepthSelector[i] = word(readEEPROM(340 + (i * 2)), readEEPROM(339 + (i * 2)));
        useLearnedPressureSelector[i] = readEEPROM(345 + i);

        for (byte j = 0; j < 5; j++) {
            for (byte k = 0; k < 8; k++) {
                buttonPrefs[i][k][j] = readEEPROM(100 + (i * 50) + (j * 10) + k);
            }
        }

        for (byte h = 0; h < 3; h++) {
            momentary[i][h] = readEEPROM(250 + (i * 3) + h);
        }

        for (byte m = 0; m < 12; m++) {
            pressureSelector[i][m] = readEEPROM(260 + m + (i * 20));
        }

        learnedPressureSelector[i] = word(readEEPROM(274 + (i * 2)), readEEPROM(273 + (i * 2)));


        pitchBendModeSelector[i] = readEEPROM(313 + i);
        breathModeSelector[i] = readEEPROM(316 + i);

        midiBendRangeSelector[i] = readEEPROM(319 + i);
        midiBendRangeSelector[i] = midiBendRangeSelector[i] > 96 ? 2 : midiBendRangeSelector[i];  // sanity check in case uninitialized

        midiChannelSelector[i] = readEEPROM(322 + i);
        midiChannelSelector[i] = midiChannelSelector[i] > 16 ? 1 : midiChannelSelector[i];  // sanity check in case uninitialized


        for (byte n = 0; n < kEXPRESSIONnVariables; n++) {
            ED[i][n] = readEEPROM(351 + n + (i * kEXPRESSIONnVariables));
        }

        for (byte p = 0; p < kIMUnVariables; p++) {
            IMUsettings[i][p] = readEEPROM(625 + p + (i * kIMUnVariables));
        }
    }
}







//load the correct user settings for the current mode (instrument). This is used at startup and any time settings are changed.
void loadPrefs() {
    vibratoHoles = vibratoHolesSelector[mode];
    octaveShift = octaveShiftSelector[mode];
    noteShift = noteShiftSelector[mode];
    pitchBendMode = pitchBendModeSelector[mode];
    useLearnedPressure = useLearnedPressureSelector[mode];
    learnedPressure = learnedPressureSelector[mode];
    senseDistance = senseDistanceSelector[mode];
    vibratoDepth = vibratoDepthSelector[mode];
    breathMode = breathModeSelector[mode];
    midiBendRange = midiBendRangeSelector[mode];
    mainMidiChannel = midiChannelSelector[mode];
    transientFilter = (pressureSelector[mode][9] + 1) / 1.25;  //this variable was formerly used for vented dropTime (unvented is now unused). Includes a correction for milliseconds

    //set these variables depending on whether "vented" is selected
    offset = pressureSelector[mode][(switches[mode][VENTED] * 6) + 0];
    multiplier = pressureSelector[mode][(switches[mode][VENTED] * 6) + 1];
    hysteresis = pressureSelector[mode][(switches[mode][VENTED] * 6) + 2];
    jumpTime = ((pressureSelector[mode][(switches[mode][VENTED] * 6) + 4]) + 1) / 1.25;  //Includes a correction for milliseconds
    dropTime = ((pressureSelector[mode][(switches[mode][VENTED] * 6) + 5]) + 1) / 1.25;  //Includes a correction for milliseconds

    //Read a custom chart from EEPROM if we're using one.
    if (modeSelector[mode] == kWARBL2Custom1 || modeSelector[mode] == kWARBL2Custom2 || modeSelector[mode] == kWARBL2Custom3 || modeSelector[mode] == kWARBL2Custom4) {
        for (int i = 0; i < 256; i++) {
            WARBL2CustomChart[i] = readEEPROM((3000 + (256 * (modeSelector[mode] - 67))) + i);
        }
    }

    pitchBend = 8192;
    expression = 0;
    shakeVibrato = 0;

    sendMIDI(PITCH_BEND, mainMidiChannel, pitchBend & 0x7F, pitchBend >> 7);

    for (byte i = 0; i < 9; i++) {
        iPitchBend[i] = 0;  //turn off pitchbend
        pitchBendOn[i] = 0;
    }

    if (switches[mode][CUSTOM] && pitchBendMode != kPitchBendNone) {
        customEnabled = 1;
    } else (customEnabled = 0);  //decide here whether custom vibrato can currently be used, so we don't have to do it every time we need to check pitchBend.

    if (switches[mode][FORCE_MAX_VELOCITY]) {
        velocity = 127;  //set velocity
    } else {
        velocity = 64;
    }

    if (!useLearnedPressure) {
        sensorThreshold[0] = (sensorCalibration + soundTriggerOffset);  //pressure sensor calibration at startup. We set the on/off threshhold just a bit higher than the reading at startup.
    }

    else {
        sensorThreshold[0] = (learnedPressure + soundTriggerOffset);
    }

    sensorThreshold[1] = sensorThreshold[0] + (offset << 2);  //threshold for move to second octave

    for (byte i = 0; i < 9; i++) {
        toneholeScale[i] = ((8 * (16383 / midiBendRange)) / (toneholeCovered[i] - 50 - senseDistance) / 2);            // Precalculate scaling factors for pitchbend. This one is for sliding. We multiply by 8 first to reduce rounding errors. We'll divide again later.
        vibratoScale[i] = ((8 * 2 * (vibratoDepth / midiBendRange)) / (toneholeCovered[i] - 50 - senseDistance) / 2);  //This one is for vibrato
    }

    adjvibdepth = vibratoDepth / midiBendRange;  //precalculations for pitchbend range
    pitchBendPerSemi = 8192 / midiBendRange;

    inputPressureBounds[0][0] = (ED[mode][INPUT_PRESSURE_MIN] * 9);  //precalculate input and output pressure ranges for sending pressure as CC
    inputPressureBounds[0][1] = (ED[mode][INPUT_PRESSURE_MAX] * 9);
    inputPressureBounds[1][0] = (ED[mode][VELOCITY_INPUT_PRESSURE_MIN] * 9);  //precalculate input and output pressure ranges for sending pressure as velocity
    inputPressureBounds[1][1] = (ED[mode][VELOCITY_INPUT_PRESSURE_MAX] * 9);
    inputPressureBounds[2][0] = (ED[mode][AFTERTOUCH_INPUT_PRESSURE_MIN] * 9);  //precalculate input and output pressure ranges for sending pressure as aftertouch
    inputPressureBounds[2][1] = (ED[mode][AFTERTOUCH_INPUT_PRESSURE_MAX] * 9);
    inputPressureBounds[3][0] = (ED[mode][POLY_INPUT_PRESSURE_MIN] * 9);  //precalculate input and output pressure ranges for sending pressure as poly
    inputPressureBounds[3][1] = (ED[mode][POLY_INPUT_PRESSURE_MAX] * 9);

    outputBounds[0][0] = ED[mode][OUTPUT_PRESSURE_MIN];  //move all these variables to a more logical order so they can be accessed in FOR loops
    outputBounds[0][1] = ED[mode][OUTPUT_PRESSURE_MAX];
    outputBounds[1][0] = ED[mode][VELOCITY_OUTPUT_PRESSURE_MIN];
    outputBounds[1][1] = ED[mode][VELOCITY_OUTPUT_PRESSURE_MAX];
    outputBounds[2][0] = ED[mode][AFTERTOUCH_OUTPUT_PRESSURE_MIN];
    outputBounds[2][1] = ED[mode][AFTERTOUCH_OUTPUT_PRESSURE_MAX];
    outputBounds[3][0] = ED[mode][POLY_OUTPUT_PRESSURE_MIN];
    outputBounds[3][1] = ED[mode][POLY_OUTPUT_PRESSURE_MAX];

    curve[0] = ED[mode][CURVE];
    curve[1] = ED[mode][VELOCITY_CURVE];
    curve[2] = ED[mode][AFTERTOUCH_CURVE];
    curve[3] = ED[mode][POLY_CURVE];

    //Roll this back for now for development
    if (IMUsettings[mode][SEND_ROLL] || IMUsettings[mode][SEND_PITCH] || IMUsettings[mode][SEND_YAW]) {
        // sox.setAccelDataRate(LSM6DS_RATE_208_HZ);  //Turn on the accel if we need it.
        // sox.setGyroDataRate(LSM6DS_RATE_208_HZ);   //Turn on the gyro if we need it.
    }

    else if (IMUsettings[mode][Y_SHAKE_PITCHBEND]) {
        // sox.setAccelDataRate(LSM6DS_RATE_208_HZ);  //Turn on only the accel for shake pitchbend (most of the IMU power is consumed by the gyro).
    }


    //Calculate upper and lower bounds for IMU pitch register mapping
    byte pitchPerRegister = (IMUsettings[mode][PITCH_REGISTER_INPUT_MAX] - IMUsettings[mode][PITCH_REGISTER_INPUT_MIN]) * 5 / IMUsettings[mode][PITCH_REGISTER_NUMBER];  //Number of degrees per register
    IMUsettings[mode][PITCH_REGISTER_NUMBER] = constrain(IMUsettings[mode][PITCH_REGISTER_NUMBER], 2, 5);                                                                //Sanity check if uninitialized. Higher values will result in writing outside of the pitchRegisterBounds[i] array.
    for (byte i = 0; i < IMUsettings[mode][PITCH_REGISTER_NUMBER] + 1; i++) {
        pitchRegisterBounds[i] = ((i * pitchPerRegister) + IMUsettings[mode][PITCH_REGISTER_INPUT_MIN] * 5) - 90;  //Upper/lower bounds for each register
    }
}






//Calibrate the sensors and store them in EEPROM
//mode 1 calibrates all sensors, mode 2 calibrates bell sensor only.
void calibrate() {
    if (!LEDon) {
        digitalWrite(LEDpins[GREEN_LED], HIGH);
        LEDon = 1;
        calibrationTimer = millis();

        if (calibration == 1) {  //calibrate all sensors if we're in calibration "mode" 1
            for (byte i = 1; i < 9; i++) {
                toneholeCovered[i] = 0;     //first set the calibration to 0 for all of the sensors so it can only be increassed by calibrating
                toneholeBaseline[i] = 255;  //and set baseline high so it can only be reduced
            }
        }
        if (bellSensor) {
            toneholeCovered[0] = 0;  //also zero the bell sensor if it's plugged in (doesn't matter which calibration mode for this one).
            toneholeBaseline[0] = 255;
        }
        return;  //we return once to make sure we've gotten some new sensor readings.
    }

    if ((calibration == 1 && ((millis() - calibrationTimer) <= 10000)) || (calibration == 2 && ((millis() - calibrationTimer) <= 5000))) {  //then set the calibration to the highest reading during the next ten seconds(or five seconds if we're only calibrating the bell sensor).
        if (calibration == 1) {
            for (byte i = 1; i < 9; i++) {
                if (toneholeCovered[i] < toneholeRead[i]) {  //covered calibration
                    toneholeCovered[i] = toneholeRead[i];
                }

                if (toneholeBaseline[i] > toneholeRead[i]) {  //baseline calibration
                    toneholeBaseline[i] = toneholeRead[i];
                }
            }
        }

        if (bellSensor && toneholeCovered[0] < toneholeRead[0]) {
            toneholeCovered[0] = toneholeRead[0];  //calibrate the bell sensor too if it's plugged in.
        }
        if (bellSensor && toneholeBaseline[0] > toneholeRead[0]) {
            toneholeBaseline[0] = toneholeRead[0];  //calibrate the bell sensor too if it's plugged in.
        }
    }

    if ((calibration == 1 && ((millis() - calibrationTimer) > 10000)) || (calibration == 2 && ((millis() - calibrationTimer) > 5000))) {
        saveCalibration();
        loadPrefs();  //do this so pitchbend scaling will be recalculated.
    }
}







//save sensor calibration (EEPROM bytes up to 34 are used (plus byte 37 to indicate a saved calibration)
void saveCalibration() {
    for (byte i = 0; i < 9; i++) {
        writeEEPROM((i + 9) * 2, highByte(toneholeCovered[i]));
        writeEEPROM(((i + 9) * 2) + 1, lowByte(toneholeCovered[i]));
        writeEEPROM((1 + i), lowByte(toneholeBaseline[i]));  //the baseline readings can be stored in a single byte because they should be close to zero.
    }
    calibration = 0;
    writeEEPROM(37, 3);  //we write a 3 to address 37 to indicate that we have stored a set of calibrations.
    digitalWrite(LEDpins[GREEN_LED], LOW);
    LEDon = 0;
}







//Load the stored sensor calibrations from EEPROM
void loadCalibration() {
    for (byte i = 0; i < 9; i++) {
        byte high = readEEPROM((i + 9) * 2);
        byte low = readEEPROM(((i + 9) * 2) + 1);
        toneholeCovered[i] = word(high, low);
        toneholeBaseline[i] = readEEPROM(1 + i);
    }
}





//Calculate pressure data for CC, velocity, channel pressure, and key pressure if those options are selected.
//Note that the time constant in the smoothing algorithm in get_sensors can be tweaked if there's too much pressure lag.
void calculatePressure(byte pressureOption) {



    long scaledPressure;

    if (pressureOption == 1) {
        scaledPressure = twelveBitPressure - (sensorThreshold[0] << 2);  //Use raw pressure for velocity so the response is as fast as possible (the low pass adds a small lag).
    }

    else {
        scaledPressure = smoothed_pressure - (sensorThreshold[0] << 2);  // 12-bit input pressure range is ~400-4000. Bring this down to 0-3600
    }


    if (scaledPressure < 0) {
        scaledPressure = 0;
    }
    scaledPressure = constrain(scaledPressure, inputPressureBounds[pressureOption][0] << 2, inputPressureBounds[pressureOption][1] << 2);     //Constrain the pressure to the input range
    scaledPressure = map(scaledPressure, inputPressureBounds[pressureOption][0] << 2, inputPressureBounds[pressureOption][1] << 2, 0, 1024);  //Scale pressure to a range of 0-1024


    if (curve[pressureOption] == 1) {  //for this curve, cube the input and scale back down.
        scaledPressure = ((scaledPressure * scaledPressure * scaledPressure) >> 20);
    }

    else if (curve[pressureOption] == 2) {  //approximates a log curve with a piecewise linear function, avoiding division
        switch (scaledPressure >> 6) {
            case 0:
                scaledPressure = scaledPressure << 3;
                break;
            case 1 ... 2:
                scaledPressure = (scaledPressure << 1) + 376;
                break;
            case 3 ... 5:
                scaledPressure = scaledPressure + 566;
                break;
            default:
                scaledPressure = (scaledPressure >> 3) + 901;
                break;
        }
        if (scaledPressure > 1024) {
            scaledPressure = 1024;
        }
    }

    //else curve 0 is linear, so no transformation

    inputPressureBounds[pressureOption][3] = (scaledPressure * (outputBounds[pressureOption][1] - outputBounds[pressureOption][0]) >> 10) + outputBounds[pressureOption][0];  //map to output pressure range


    if (pressureOption == 1) {  //set velocity to mapped pressure if desired
        velocity = inputPressureBounds[pressureOption][3];
        if (velocity == 0) {  //use a minumum of 1 for velocity so a note is still turned on (changed in v. 2.1)
            velocity = 1;
        }
    }
}








//send pressure data
void sendPressure(bool force) {
    if (ED[mode][SEND_PRESSURE] == 1 && (inputPressureBounds[0][3] != prevCCPressure || force)) {
        sendMIDI(CC, ED[mode][PRESSURE_CHANNEL], ED[mode][PRESSURE_CC], inputPressureBounds[0][3]);  //send MSB of pressure mapped to the output range
        prevCCPressure = inputPressureBounds[0][3];
    }

    if ((switches[mode][SEND_AFTERTOUCH] & 1)) {
        // hack
        int sendm = (!noteon && sensorValue <= 100) ? 0 : inputPressureBounds[2][3];
        if (sendm != prevChanPressure || force) {
            sendMIDI(CHANNEL_PRESSURE, mainMidiChannel, sendm);  //send MSB of pressure mapped to the output range
            prevChanPressure = sendm;
        }
    }

    // poly aftertouch uses 2nd lowest bit of ED flag
    if ((switches[mode][SEND_AFTERTOUCH] & 2) && noteon) {
        // hack
        int sendm = (!noteon && sensorValue <= 100) ? 0 : inputPressureBounds[3][3];
        if (sendm != prevPolyPressure || force) {
            sendMIDI(KEY_PRESSURE, mainMidiChannel, notePlaying, sendm);  //send MSB of pressure mapped to the output range
            prevPolyPressure = sendm;
        }
    }
}









//Starting advertising BLE
void startAdv(void) {
    // Set General Discoverable Mode flag
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);

    // Advertise TX Power
    Bluefruit.Advertising.addTxPower();

    // Advertise BLE MIDI Service
    Bluefruit.Advertising.addService(blemidi);

    // Secondary Scan Response packet (optional)
    // Since there is no room for 'Name' in Advertising packet
    Bluefruit.ScanResponse.addName();

    /* Start Advertising
   * - Enable auto advertising if disconnected
   * - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
   * - Timeout for fast mode is 30 seconds
   * - Start(timeout) with timeout = 0 will advertise forever (until connected)
   *
   * For recommended advertising interval
   * https://developer.apple.com/library/content/qa/qa1931/_index.html   
   */
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);  // in unit of 0.625 ms -- these are the settings recommended by Apple (20 ms and 152.5 ms)
    Bluefruit.Advertising.setFastTimeout(30);    // number of seconds in fast mode
    Bluefruit.Advertising.start(0);              // 0 = Don't stop advertising after n seconds
}









//These convert MIDI messages from the old WARBL code to the correct format for the MIDI.h library. ToDo: These could be made shorter/more efficient.
void sendMIDI(uint8_t m, uint8_t c, uint8_t d1, uint8_t d2)  // send a 3-byte MIDI event over USB
{
    m &= 0xF0;
    c &= 0xF;
    d1 &= 0x7F;
    d2 &= 0x7F;


    switch (m) {

        case 0x80:  //Note Off
            {
                if (WARBL2settings[MIDI_DESTINATION] != 1 || connIntvl == 0) {  //If we're not only sending BLE or if we're not connected to BLE
                    MIDI.sendNoteOff(d1, d2, c);                                //Send USB MIDI.
                }
                if (WARBL2settings[MIDI_DESTINATION] != 0 || USBstatus != USB_HOST) {  //If we're not only sending USB or if we're not connected to USB
                    BLEMIDI.sendNoteOff(d1, d2, c);                                    //Send BLE MIDI.
                }
                break;
            }
        case 0x90:  //Note On
            {
                if (WARBL2settings[MIDI_DESTINATION] != 1 || connIntvl == 0) {
                    MIDI.sendNoteOn(d1, d2, c);
                }
                if (WARBL2settings[MIDI_DESTINATION] != 0 || USBstatus != USB_HOST) {
                    BLEMIDI.sendNoteOn(d1, d2, c);
                }
                break;
            }
        case 0xA0:  //Key Pressure
            {
                if (WARBL2settings[MIDI_DESTINATION] != 1 || connIntvl == 0) {
                    MIDI.sendAfterTouch(d1, d2, c);
                }
                if (WARBL2settings[MIDI_DESTINATION] != 0 || USBstatus != USB_HOST) {
                    BLEMIDI.sendAfterTouch(d1, d2, c);
                }
                break;
            }
        case 0xB0:  //CC
            {
                if (WARBL2settings[MIDI_DESTINATION] != 1 || connIntvl == 0) {
                    MIDI.sendControlChange(d1, d2, c);
                }
                if (WARBL2settings[MIDI_DESTINATION] != 0 || USBstatus != USB_HOST) {
                    BLEMIDI.sendControlChange(d1, d2, c);
                }
                break;
            }
        case 0xC0:  //Program Change
            {
                break;
            }
        case 0xD0:  //Channel Pressure
            {
                break;
            }
        case 0xE0:  //Pitchbend
            {
                int16_t pitch = ((d2 << 7) + d1) - 8192;
                if (WARBL2settings[MIDI_DESTINATION] != 1 || connIntvl == 0) {
                    MIDI.sendPitchBend(pitch, c);
                }
                if (WARBL2settings[MIDI_DESTINATION] != 0 || USBstatus != USB_HOST) {
                    BLEMIDI.sendPitchBend(pitch, c);
                }
                break;
            }
        default:
            {
                break;
            }
    }
}










void sendMIDI(uint8_t m, uint8_t c, uint8_t d)  // send a 2-byte MIDI event over USB
{
    m &= 0xF0;
    c &= 0xF;
    d &= 0x7F;

    switch (m) {

        case 0xD0:  //Channel pressure
            {
                if (WARBL2settings[MIDI_DESTINATION] != 1 || connIntvl == 0) {
                    MIDI.sendAfterTouch(d, c);
                    //MIDI.sendPolyPressure(d, c); //deprecated
                }
                if (WARBL2settings[MIDI_DESTINATION] != 0 || USBstatus != USB_HOST) {
                    BLEMIDI.sendAfterTouch(d, c);
                    //BLEMIDI.sendPolyPressure(d, c); //deprecated
                }
                break;
            }


        case 0xC0:  //Program Change
            {
                if (WARBL2settings[MIDI_DESTINATION] != 1 || connIntvl == 0) {
                    MIDI.sendProgramChange(d, c);
                }
                if (WARBL2settings[MIDI_DESTINATION] != 0 || USBstatus != USB_HOST) {
                    BLEMIDI.sendProgramChange(d, c);
                }
                break;
            }
    }
}









//retrieve BLE connection information
void connect_callback(uint16_t conn_handle) {
    // Get the reference to current connection
    BLEConnection* connection = Bluefruit.Connection(conn_handle);

    char central_name[32] = { 0 };

    connection->getPeerName(central_name, sizeof(central_name));

    // Get the current connection agreed upon connection interval in units of 0.625 ms
    connIntvl = connection->getConnectionInterval();
    connIntvl = connIntvl * 0.625;  //convert to ms

    if (communicationMode) {
        sendMIDI(CC, 7, 106, 72);
        sendMIDI(CC, 7, 119, (connIntvl * 100) & 0x7F);  //Send LSB of the connection interval to Config Tool.
        sendMIDI(CC, 7, 106, 73);
        sendMIDI(CC, 7, 119, (connIntvl * 100) >> 7);  //Send MSB of the connection interval to Conf Tool.
    }
}








//Detect BLE disconnect
void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
    (void)conn_handle;
    (void)reason;

    connIntvl = 0;

    if (communicationMode) {
        sendMIDI(CC, 7, 106, 72);
        sendMIDI(CC, 7, 119, 0);  //Send 0 to Config Tool.
        sendMIDI(CC, 7, 106, 73);
        sendMIDI(CC, 7, 119, 0);  //Send 0 to Config Tool.
    }
}








//Watchdog enable, taken from Adafruit Sleepydog library
void watchdog_enable(int maxPeriodMS) {
    if (maxPeriodMS < 0)
        return;

    // cannot change wdt config register once it is started
    if (nrf_wdt_started(NRF_WDT))
        return;

    // WDT run when CPU is asleep
    nrf_wdt_behaviour_set(NRF_WDT, NRF_WDT_BEHAVIOUR_RUN_SLEEP);
    nrf_wdt_reload_value_set(NRF_WDT, (maxPeriodMS * 32768) / 1000);

    // use channel 0
    nrf_wdt_reload_request_enable(NRF_WDT, NRF_WDT_RR0);

    // Start WDT
    // After started CRV, RREN and CONFIG is blocked
    // There is no way to stop/disable watchdog using source code
    // It can only be reset by WDT timeout, Pin reset, Power reset
    nrf_wdt_task_trigger(NRF_WDT, NRF_WDT_TASK_START);
}







//Watchdog reset
void watchdog_reset() {
    nrf_wdt_reload_request_set(NRF_WDT, NRF_WDT_RR0);
}







//EEPROM functions

// Write to EEPROOM
void writeEEPROM(int address, byte val) {

    if (address < 16384) {                 //Make sure we're not trying to write beyond the highest address in the M24128 EEPROM.
        if (val != readEEPROM(address)) {  //Don't write if the value is already there.
            Wire.beginTransmission(EEPROM_I2C_ADDRESS);
            Wire.write((int)(address >> 8));    //writes the MSB
            Wire.write((int)(address & 0xFF));  //writes the LSB
            Wire.write(val);
            Wire.endTransmission();
            while (EEPROMbusy() == true) {  //Poll until the previous transmission has completed.
                delayMicroseconds(200);
            }
        }
    }
}


// Read from EEPROM
byte readEEPROM(int address) {

    byte rcvData = 0xFF;
    Wire.beginTransmission(EEPROM_I2C_ADDRESS);
    Wire.write((int)(address >> 8));    //writes the MSB
    Wire.write((int)(address & 0xFF));  //writes the LSB
    Wire.endTransmission();
    Wire.requestFrom(EEPROM_I2C_ADDRESS, 1);
    rcvData = Wire.read();
    while (EEPROMbusy() == true) {  //Poll until the previous transmission has completed.
        delayMicroseconds(200);
    }
    return rcvData;
}


// ACK polling to see if previous EEPROM transaction has finished
bool EEPROMbusy(void) {

    Wire.beginTransmission(EEPROM_I2C_ADDRESS);
    if (Wire.endTransmission() == 0) {
        return (false);
    }
    return (true);
}


// Put an int to EEPROM
void putEEPROM(int address, int val) {

    byte byte1 = val >> 8;
    byte byte2 = val & 0xFF;
    writeEEPROM(address, byte1);
    writeEEPROM(address + 1, byte2);
}


// Get an int from EEPROM
void getEEPROM(int address, int& var) {

    byte byte1 = readEEPROM(address);
    byte byte2 = readEEPROM(address + 1);
    var = (byte1 << 8) + byte2;
}


// Put a float to EEPROM
void putEEPROM(int address, float value) {

    byte* p = (byte*)(void*)&value;
    for (int i = 0; i < sizeof(value); i++)
        writeEEPROM(address++, *p++);
}


// Get a float from EEPROM
void getEEPROM(int address, float& var) {

    float value = 0.0;
    byte* p = (byte*)(void*)&value;
    for (int i = 0; i < sizeof(value); i++)
        *p++ = readEEPROM(address++);
    var = value;
}


// For testing purposes
void eraseEEPROM(void) {

    for (int i = 1; i < 16384; i++) {
        writeEEPROM(i, 255);
    }
}
