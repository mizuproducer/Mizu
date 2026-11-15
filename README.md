# Mizu

A phone app that reads **pH** and **total alkalinity** from a water tester and tells you what to add to fix your water. Works for swimming pools, hot tubs, freshwater and reef aquariums, and drinking/well water.

## What's here

| File | What it is |
| --- | --- |
| `www/index.html` | The app itself. Used for both the Android app and the web version. |
| `.github/workflows/android.yml` | Builds the Android APK on GitHub's computers. |
| `package.json`, `capacitor.config.json` | Settings that turn the web app into an Android app (Capacitor). |
| `android-res/` | The Mizu app icon. |
| `firmware/mizu_device/mizu_device.ino` | Code for an ESP32-based tester that talks to the app. |

## How the connection works

| Mode | Range | How |
| --- | --- | --- |
| **Bluetooth** | ~10–100 m (more with an external-antenna ESP32-WROOM-32U, line of sight) | Phone pairs directly with the tester. |
| **Remote** | Anywhere with internet | Tester joins Wi-Fi and sends readings to an MQTT server; the app reads them from there. |
| **Demo** | — | Simulated tester, or type numbers from a test kit. |

Bluetooth alone can't cover truly long distances, so the tester uses Bluetooth for nearby pairing and setup, and Wi-Fi + internet for long distance. Set Wi-Fi up once from the app: connect over Bluetooth → **Set up long-distance mode** → enter your Wi-Fi name and password.

If the tester is somewhere with no Wi-Fi (a far pond, a field), swap the Wi-Fi part for a cellular (LTE-M) or LoRa module that publishes the same JSON to the same MQTT topic. The app doesn't need to change.

## Getting the Android app (APK)

GitHub builds the APK for you for free. Nothing needs to be installed on your computer.

1. Make a free account at github.com.
2. Click **+** → **New repository**, name it `mizu`, and create it.
3. Click **uploading an existing file** and drag in **everything inside the `mizu` folder**, including the hidden `.github` folder. (If your file window hides it, turn on "Show hidden items" in Windows Explorer's View menu.) Click **Commit changes**.
4. Open the **Actions** tab. The "Build Android APK" job starts by itself and takes about 5 minutes.
5. When it shows a green tick, open **Releases** on the repository's main page and download **Mizu.apk**. Open that page on your phone to download the file straight to it.
6. On the phone, open the file. Android will ask to allow installing apps from your browser or file manager; allow it, then tap **Install**.
7. Open Mizu, tap **Connect over Bluetooth**, and allow the Bluetooth permission.

Every time you change a file in the repository, a new APK is built automatically.

This is a test ("debug") build, which is fine for your own phones. Publishing on the Google Play Store needs a signed release build and a Google Play developer account ($25 one-time).

### Building on your own computer instead (optional)

Install Node.js 22, Java 21 and Android Studio, then in this folder run:

```
npm install
npx cap add android
cp -r android-res/. android/app/src/main/res/
npm run apk
```

The APK appears at `android/app/build/outputs/apk/debug/app-debug.apk`.

## Using it in a browser instead

The same `www/index.html` also works as a web page over **https** (for example on GitHub Pages). On Android, use Chrome. On iPhone, Safari has no Bluetooth, so use the free **Bluefy** browser; Remote mode works in Safari.
## Building the tester

- ESP32 DevKit (or ESP32-WROOM-32U + antenna for best Bluetooth range)
- Analog pH probe board (e.g. DFRobot Gravity pH V2) → GPIO 34
- Alkalinity: simple probes can't measure it; it needs a titration-based alkalinity analyzer. The firmware reads `TA:<ppm>` lines from one on Serial2 (GPIO 16). Change `readAlkalinity()` to match yours.

Arduino IDE: install the ESP32 board package, plus the **NimBLE-Arduino** (1.4.x) and **PubSubClient** libraries, then upload. Calibrate the pH probe by putting it in pH 7.00 and 4.00 buffer and entering the voltages at the top of the sketch. The tester's ID (like `MZ-1A2B3C`) prints on the serial monitor; you'll need it for Remote mode.

## Privacy

The default server (`broker.hivemq.com`) is public: anyone who knows your tester ID can see its readings. For private use, run your own MQTT server (Mosquitto, HiveMQ Cloud free tier, etc.) and change it in the firmware and in the app under *Remote → Server settings*.

## About the advice

Dosing amounts are standard rule-of-thumb estimates (e.g. 1.4 lb baking soda per 10,000 gal raises alkalinity ~10 ppm). Add about half, circulate, retest, then add the rest. Drinking-water advice suggests filters and lab tests only; it never suggests adding chemicals.
