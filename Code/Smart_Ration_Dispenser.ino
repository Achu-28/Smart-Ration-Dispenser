#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include "HX711.h"

// ============================================================
//                 SMART RATION DISPENSER
// ============================================================
// Customer Display:
//      http://192.168.4.1/
//
// Admin Dashboard:
//      http://192.168.4.1/dashboard
//
// WiFi:
//      SSID     : SMART-RATION
//      Password : ration123
// ============================================================


// ============================================================
// WIFI
// ============================================================

const char* WIFI_SSID = "SMART-RATION";
const char* WIFI_PASSWORD = "ration123";

WebServer server(80);


// ============================================================
// PIN CONFIGURATION
// ============================================================

// Servos
const int RICE_SERVO_PIN = 25;
const int SUGAR_SERVO_PIN = 26;

// HX711
const int HX711_DOUT_PIN = 32;
const int HX711_SCK_PIN = 33;

// LEDs
const int GREEN_LED_PIN = 27;
const int RED_LED_PIN = 14;

// IR sensor
const int IR_SENSOR_PIN = 13;


// ============================================================
// IR SENSOR CONFIGURATION
// ============================================================
//
// Most common IR obstacle/proximity modules:
// LOW  = object detected
// HIGH = no object
//
// If your sensor behaves opposite, change:
// true  -> false
// ============================================================

const bool IR_ACTIVE_LOW = false;


// ============================================================
// SERVO SETTINGS
// ============================================================
//
// These are starting angles.
// Physically adjust them if your mechanism needs different
// angles.
// ============================================================

Servo riceServo;
Servo sugarServo;

const int RICE_CLOSED_ANGLE = 0;
const int RICE_OPEN_ANGLE = 180;

const int SUGAR_CLOSED_ANGLE = 0;
const int SUGAR_OPEN_ANGLE = 180;


// ============================================================
// HX711
// ============================================================

HX711 scale;

// IMPORTANT:
// This is only a starting calibration factor.
// You MUST calibrate your own load cell for accurate grams.
float calibration_factor = -7050.0;


// ============================================================
// DISPENSING SETTINGS
// ============================================================

// User-entered dispensing quantity
float requestedWeight = 500.0;

// Demo dispensing time
const unsigned long DEMO_DISPENSE_TIME = 8000;


// ============================================================
// USERS
// ============================================================

struct User
{
  String pin;
  String name;

  float riceEntitlement;
  float sugarEntitlement;
};


User users[3] =
{
  {
    "1234",
    "User 01",
    5000.0,
    2000.0
  },

  {
    "5678",
    "User 02",
    5000.0,
    2000.0
  },

  {
    "2468",
    "User 03",
    5000.0,
    2000.0
  }
};


// ============================================================
// LOGIN / USER STATE
// ============================================================

int currentUser = -1;


// ============================================================
// DISPENSING STATE
// ============================================================

bool dispensing = false;

String selectedItem = "";

float currentWeight = 0.0;

unsigned long dispenseStartTime = 0;


// ============================================================
// LAST TRANSACTION STATUS
// ============================================================

bool lastDispenseSuccess = true;

String lastTransactionMessage = "System ready";

String lastTransactionUser = "-";

String lastTransactionItem = "-";

float lastTransactionWeight = 0.0;


// ============================================================
// TRANSACTION HISTORY
// ============================================================

const int MAX_HISTORY = 10;

struct Transaction
{
  String user;
  String item;
  float weight;
  bool success;
};

Transaction historyList[MAX_HISTORY];

int historyCount = 0;


// ============================================================
// LED FUNCTIONS
// ============================================================

void setGreenLED(bool state)
{
  digitalWrite(
    GREEN_LED_PIN,
    state ? HIGH : LOW
  );
}


void setRedLED(bool state)
{
  digitalWrite(
    RED_LED_PIN,
    state ? HIGH : LOW
  );
}


void allLEDsOff()
{
  setGreenLED(false);
  setRedLED(false);
}


void errorBlink()
{
  setRedLED(true);

  delay(300);

  setRedLED(false);
}


// ============================================================
// IR SENSOR
// ============================================================

bool isIRDetected()
{
  int state = digitalRead(IR_SENSOR_PIN);

  if (IR_ACTIVE_LOW)
  {
    return state == LOW;
  }

  return state == HIGH;
}


// ============================================================
// SERVO FUNCTIONS
// ============================================================

void closeRiceServo()
{
  riceServo.write(RICE_CLOSED_ANGLE);
}


void openRiceServo()
{
  riceServo.write(RICE_OPEN_ANGLE);
}


void closeSugarServo()
{
  sugarServo.write(SUGAR_CLOSED_ANGLE);
}


void openSugarServo()
{
  sugarServo.write(SUGAR_OPEN_ANGLE);
}


void closeAllServos()
{
  closeRiceServo();
  closeSugarServo();
}


// ============================================================
// TRANSACTION HISTORY
// ============================================================

void addTransaction(
  String user,
  String item,
  float weight,
  bool success
)
{
  if (historyCount < MAX_HISTORY)
  {
    historyList[historyCount].user = user;
    historyList[historyCount].item = item;
    historyList[historyCount].weight = weight;
    historyList[historyCount].success = success;

    historyCount++;
  }
  else
  {
    for (int i = 0; i < MAX_HISTORY - 1; i++)
    {
      historyList[i] = historyList[i + 1];
    }

    historyList[MAX_HISTORY - 1].user = user;
    historyList[MAX_HISTORY - 1].item = item;
    historyList[MAX_HISTORY - 1].weight = weight;
    historyList[MAX_HISTORY - 1].success = success;
  }
}


// ============================================================
// CUSTOMER DISPLAY HTML
// ============================================================

String customerPage()
{
  String html = R"rawliteral(
<!DOCTYPE html>

<html>

<head>

<meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1">

<title>Smart Ration</title>

<style>

*{
  box-sizing:border-box;
}

body{
  margin:0;
  font-family:Arial,Helvetica,sans-serif;
  background:#0f172a;
  color:white;
}

.container{
  width:92%;
  max-width:480px;
  margin:auto;
  padding:20px 0 40px;
}

.header{
  text-align:center;
  margin-bottom:20px;
}

.logo{
  font-size:30px;
  font-weight:bold;
}

.subtitle{
  color:#94a3b8;
  margin-top:6px;
}

.card{
  background:#1e293b;
  border-radius:18px;
  padding:20px;
  margin-bottom:16px;
  box-shadow:0 8px 25px rgba(0,0,0,.25);
}

h2{
  margin-top:0;
}

.center{
  text-align:center;
}

.language-button{
  width:100%;
  padding:18px;
  margin:8px 0;
  border:0;
  border-radius:14px;
  font-size:18px;
  font-weight:bold;
  background:#334155;
  color:white;
}

.keypad{
  display:grid;
  grid-template-columns:repeat(3,1fr);
  gap:8px;
}

.keypad button{
  padding:17px;
  border:0;
  border-radius:12px;
  background:#334155;
  color:white;
  font-size:22px;
  font-weight:bold;
}

.pin-display{
  text-align:center;
  font-size:28px;
  letter-spacing:8px;
  min-height:45px;
  margin:10px;
}

.main-button{
  width:100%;
  padding:17px;
  border:0;
  border-radius:13px;
  font-size:17px;
  font-weight:bold;
  margin-top:10px;
  color:white;
}

.rice{
  background:#f59e0b;
}

.sugar{
  background:#38bdf8;
}

.green{
  background:#22c55e;
}

.red{
  background:#ef4444;
}

.back{
  background:#475569;
}

.item-box{
  padding:20px;
  border-radius:14px;
  margin:10px 0;
  text-align:center;
  font-size:20px;
  font-weight:bold;
}

.entitlement{
  background:#0f172a;
  padding:15px;
  border-radius:13px;
}

.row{
  display:flex;
  justify-content:space-between;
  padding:9px 0;
  border-bottom:1px solid #334155;
}

.row:last-child{
  border-bottom:0;
}

.weight{
  text-align:center;
  font-size:45px;
  font-weight:bold;
  margin:15px 0;
}

.progress-container{
  width:100%;
  height:22px;
  background:#334155;
  border-radius:20px;
  overflow:hidden;
}

.progress{
  width:0%;
  height:100%;
  background:#22c55e;
  transition:.3s;
}

.message{
  text-align:center;
  margin-top:15px;
  color:#cbd5e1;
}

.success{
  color:#4ade80;
}

.error{
  color:#f87171;
}

.hidden{
  display:none;
}

.small{
  color:#94a3b8;
  font-size:14px;
}

</style>

</head>


<body>


<div class="container">


<div class="header">

<div class="logo">
SMART RATION
</div>

<div class="subtitle">
Smart Dispensing System
</div>

</div>


<!-- ================================================= -->
<!-- LANGUAGE -->
<!-- ================================================= -->

<div id="languagePage">

<div class="card">

<h2 class="center">
Select Language
</h2>

<button
class="language-button"
onclick="selectLanguage('English')">

🇬🇧 English

</button>

<button
class="language-button"
onclick="selectLanguage('Tamil')">

தமிழ் Tamil

</button>

</div>

</div>


<!-- ================================================= -->
<!-- LOGIN -->
<!-- ================================================= -->

<div id="loginPage" class="hidden">

<div class="card">

<h2 class="center">
🔐 User Authentication
</h2>

<p class="small center">
Enter your 4-digit PIN
</p>

<div id="pinDisplay" class="pin-display">
○ ○ ○ ○
</div>


<div class="keypad">

<button onclick="addDigit('1')">1</button>
<button onclick="addDigit('2')">2</button>
<button onclick="addDigit('3')">3</button>

<button onclick="addDigit('4')">4</button>
<button onclick="addDigit('5')">5</button>
<button onclick="addDigit('6')">6</button>

<button onclick="addDigit('7')">7</button>
<button onclick="addDigit('8')">8</button>
<button onclick="addDigit('9')">9</button>

<button onclick="clearPin()">C</button>
<button onclick="addDigit('0')">0</button>
<button onclick="backspace()">⌫</button>

</div>


<button
class="main-button green"
onclick="login()">

LOGIN

</button>


<p id="loginMessage" class="message"></p>

</div>

</div>


<!-- ================================================= -->
<!-- USER HOME -->
<!-- ================================================= -->

<div id="homePage" class="hidden">

<div class="card">

<h2>
👤 Welcome
</h2>

<div class="row">

<span>User</span>

<strong id="userName">
-
</strong>

</div>

</div>


<div class="card">

<h2>
📦 Your Entitlement
</h2>

<div class="entitlement">

<div class="row">

<span>🍚 Rice</span>

<strong>
<span id="riceRemaining">0</span> g
</strong>

</div>


<div class="row">

<span>🍬 Sugar</span>

<strong>
<span id="sugarRemaining">0</span> g
</strong>

</div>

</div>

</div>


<div class="card">

<h2>
Select Item
</h2>

<button
class="main-button rice"
onclick="selectItem('rice')">

🍚 RICE

</button>


<button
class="main-button sugar"
onclick="selectItem('sugar')">

🍬 SUGAR

</button>

</div>

</div>


<!-- ================================================= -->
<!-- WEIGHT SELECTION -->
<!-- ================================================= -->

<div id="weightPage" class="hidden">

<div class="card">

<h2 class="center">
Select Weight
</h2>

<p class="small center">
Enter dispensing quantity in grams
</p>

<div class="item-box">
<span id="selectedItemText">
-
</span>
</div>

<input
id="amountInput"
type="number"
min="1"
step="1"
placeholder="Enter grams"
style="width:100%;padding:17px;border:0;border-radius:13px;font-size:20px;text-align:center;box-sizing:border-box;margin:10px 0;background:#334155;color:white;">

<button
class="main-button green"
onclick="startDispensing()">

DISPENSE

</button>

<button
class="main-button back"
onclick="backToHome()">

BACK

</button>

</div>

</div>


<!-- ================================================= -->
<!-- DISPENSING -->
<!-- ================================================= -->

<div id="dispensingPage" class="hidden">

<div class="card">

<h2 class="center">
⏳ Dispensing
</h2>

<p class="center" id="dispensingText">
Please wait...
</p>

<div class="weight">

<span id="currentWeight">
0
</span> g

</div>


<div class="progress-container">

<div
id="progressBar"
class="progress">
</div>

</div>


<p
id="dispensingMessage"
class="message">

Dispensing in progress...

</p>

</div>

</div>


<!-- ================================================= -->
<!-- COMPLETE -->
<!-- ================================================= -->

<div id="completePage" class="hidden">

<div class="card">

<h2 class="center success">
✓ Dispensing Complete
</h2>

<p class="center">
Your ration has been dispensed successfully.
</p>

<div class="item-box">

<span id="completedItem">
-
</span>

<br><br>

<span id="completedWeight">0</span> g

</div>


<button
class="main-button green"
onclick="continueAfterDispense()">

CONTINUE

</button>

</div>

</div>


<!-- ================================================= -->
<!-- ERROR -->
<!-- ================================================= -->

<div id="errorPage" class="hidden">

<div class="card">

<h2 class="center error">
✕ Dispensing Error
</h2>

<p
id="errorMessage"
class="center">

Dispensing could not be completed.

</p>


<button
class="main-button back"
onclick="continueAfterDispense()">

BACK

</button>

</div>

</div>


</div>


<script>


// =====================================================
// VARIABLES
// =====================================================

let pin = "";

let selectedLanguage = "English";

let selectedItem = "";

let polling = false;


// =====================================================
// PAGE CONTROL
// =====================================================

function showPage(pageId)
{
  const pages = [
    "languagePage",
    "loginPage",
    "homePage",
    "weightPage",
    "dispensingPage",
    "completePage",
    "errorPage"
  ];

  pages.forEach(function(page)
  {
    document
      .getElementById(page)
      .classList.add("hidden");
  });

  document
    .getElementById(pageId)
    .classList.remove("hidden");
}


// =====================================================
// LANGUAGE
// =====================================================

function selectLanguage(language)
{
  selectedLanguage = language;

  showPage("loginPage");
}


// =====================================================
// PIN
// =====================================================

function updatePinDisplay()
{
  let text = "";

  for(let i = 0; i < 4; i++)
  {
    if(i < pin.length)
    {
      text += "● ";
    }
    else
    {
      text += "○ ";
    }
  }

  document
    .getElementById("pinDisplay")
    .innerText = text;
}


function addDigit(digit)
{
  if(pin.length < 4)
  {
    pin += digit;
    updatePinDisplay();
  }
}


function clearPin()
{
  pin = "";
  updatePinDisplay();
}


function backspace()
{
  if(pin.length > 0)
  {
    pin = pin.substring(
      0,
      pin.length - 1
    );

    updatePinDisplay();
  }
}


// =====================================================
// LOGIN
// =====================================================

function login()
{
  if(pin.length != 4)
  {
    document
      .getElementById("loginMessage")
      .innerText = "Enter a 4-digit PIN";

    return;
  }


  fetch(
    "/api/login",
    {
      method:"POST",

      headers:
      {
        "Content-Type":
        "application/x-www-form-urlencoded"
      },

      body:
      "pin=" + encodeURIComponent(pin)
    }
  )

  .then(response => response.json())

  .then(data =>
  {
    if(data.success)
    {
      document
        .getElementById("userName")
        .innerText = data.name;

      document
        .getElementById("riceRemaining")
        .innerText =
        Math.round(data.rice);

      document
        .getElementById("sugarRemaining")
        .innerText =
        Math.round(data.sugar);

      pin = "";

      updatePinDisplay();

      showPage("homePage");
    }
    else
    {
      document
        .getElementById("loginMessage")
        .innerText = "Invalid PIN";

      pin = "";

      updatePinDisplay();
    }
  })

  .catch(error =>
  {
    document
      .getElementById("loginMessage")
      .innerText =
      "Connection error";
  });
}


// =====================================================
// SELECT ITEM
// =====================================================

function selectItem(item)
{
  selectedItem = item;

  document
    .getElementById("selectedItemText")
    .innerText =
    item.toUpperCase();

  document
    .getElementById("amountInput")
    .value = "";

  showPage("weightPage");
}


// =====================================================
// BACK TO HOME
// =====================================================

function backToHome()
{
  showPage("homePage");
}


// =====================================================
// START DISPENSING
// =====================================================

function startDispensing()
{
  if(polling)
  {
    return;
  }

  let amount =
    parseFloat(
      document.getElementById("amountInput").value
    );

  if(!amount || amount <= 0)
  {
    alert("Enter a valid amount in grams");
    return;
  }


  fetch(
    "/api/dispense",
    {
      method:"POST",

      headers:
      {
        "Content-Type":
        "application/x-www-form-urlencoded"
      },

      body:
      "item=" +
      encodeURIComponent(selectedItem) +
      "&amount=" +
      encodeURIComponent(amount)
    }
  )

  .then(response => response.json())

  .then(data =>
  {
    if(data.success)
    {
      document
        .getElementById("dispensingText")
        .innerText =
        "Dispensing " +
        selectedItem.toUpperCase();

      document
        .getElementById("currentWeight")
        .innerText = "0";

      document
        .getElementById("progressBar")
        .style.width = "0%";

      document
        .getElementById("dispensingMessage")
        .innerText =
        "Please wait...";

      polling = true;

      showPage("dispensingPage");

      pollDispensing();
    }
    else
    {
      alert(data.message);
    }
  })

  .catch(error =>
  {
    alert("Connection error");
  });
}


// =====================================================
// DISPENSING STATUS
// =====================================================

function pollDispensing()
{
  fetch("/api/dispense-status")

  .then(response => response.json())

  .then(data =>
  {
    document
      .getElementById("currentWeight")
      .innerText =
      Math.round(data.weight);


    let percentage =
      (data.weight / data.target) * 100.0;


    if(percentage < 0)
    {
      percentage = 0;
    }


    if(percentage > 100)
    {
      percentage = 100;
    }


    document
      .getElementById("progressBar")
      .style.width =
      percentage + "%";


    if(data.finished)
    {
      polling = false;


      if(data.success)
      {
        document
          .getElementById("completedItem")
          .innerText =
          selectedItem.toUpperCase();

        document
          .getElementById("completedWeight")
          .innerText =
          Math.round(data.weight);


        showPage("completePage");
      }
      else
      {
        document
          .getElementById("errorMessage")
          .innerText =
          data.message;

        showPage("errorPage");
      }

      return;
    }


    setTimeout(
      pollDispensing,
      300
    );
  })

  .catch(error =>
  {
    polling = false;

    document
      .getElementById("errorMessage")
      .innerText =
      "Connection to ESP32 lost.";

    showPage("errorPage");
  });
}


// =====================================================
// CONTINUE AFTER DISPENSING
// =====================================================

function continueAfterDispense()
{
  selectedItem = "";

  updateUserState();

  showPage("homePage");
}


// =====================================================
// UPDATE USER STATE
// =====================================================

function updateUserState()
{
  fetch("/api/state")

  .then(response => response.json())

  .then(data =>
  {
    if(data.loggedIn)
    {
      document
        .getElementById("userName")
        .innerText =
        data.name;

      document
        .getElementById("riceRemaining")
        .innerText =
        Math.round(data.rice);

      document
        .getElementById("sugarRemaining")
        .innerText =
        Math.round(data.sugar);
    }
  });
}


// =====================================================
// INITIALIZE
// =====================================================

updatePinDisplay();

</script>

</body>

</html>
)rawliteral";

  return html;
}


// ============================================================
// ADMIN DASHBOARD HTML
// ============================================================

String adminPage()
{
  String html = R"rawliteral(
<!DOCTYPE html>

<html>

<head>

<meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1">

<title>Smart Ration Admin</title>

<style>

*{
  box-sizing:border-box;
}

body{
  margin:0;
  font-family:Arial,Helvetica,sans-serif;
  background:#0f172a;
  color:white;
}

.container{
  width:94%;
  max-width:1000px;
  margin:auto;
  padding:20px 0 40px;
}

.header{
  text-align:center;
  margin-bottom:20px;
}

.logo{
  font-size:30px;
  font-weight:bold;
}

.subtitle{
  color:#94a3b8;
}

.grid{
  display:grid;
  grid-template-columns:repeat(auto-fit,minmax(220px,1fr));
  gap:15px;
}

.card{
  background:#1e293b;
  border-radius:18px;
  padding:20px;
  box-shadow:0 8px 25px rgba(0,0,0,.25);
  margin-bottom:15px;
}

.card h2{
  margin-top:0;
}

.status{
  display:flex;
  justify-content:space-between;
  padding:12px 0;
  border-bottom:1px solid #334155;
}

.status:last-child{
  border-bottom:0;
}

.good{
  color:#4ade80;
  font-weight:bold;
}

.bad{
  color:#f87171;
  font-weight:bold;
}

.warning{
  color:#facc15;
  font-weight:bold;
}

.value{
  font-size:28px;
  font-weight:bold;
  margin-top:8px;
}

.history{
  overflow-x:auto;
}

table{
  width:100%;
  border-collapse:collapse;
}

th,td{
  text-align:left;
  padding:10px;
  border-bottom:1px solid #334155;
}

th{
  color:#94a3b8;
}

.success{
  color:#4ade80;
  font-weight:bold;
}

.failed{
  color:#f87171;
  font-weight:bold;
}

.small{
  color:#94a3b8;
  font-size:13px;
}

</style>

</head>


<body>


<div class="container">


<div class="header">

<div class="logo">
SMART RATION
</div>

<div class="subtitle">
ADMIN MONITORING DASHBOARD
</div>

</div>


<!-- ================================================= -->
<!-- SYSTEM STATUS -->
<!-- ================================================= -->

<div class="card">

<h2>⚙️ System Status</h2>


<div class="status">

<span>Wi-Fi</span>

<span
id="wifi"
class="good">

ONLINE

</span>

</div>


<div class="status">

<span>System</span>

<span
id="system"
class="good">

READY

</span>

</div>


<div class="status">

<span>IR Sensor</span>

<span id="ir">
CLEAR
</span>

</div>


<div class="status">

<span>Green LED</span>

<span id="green">
OFF
</span>

</div>


<div class="status">

<span>Red LED</span>

<span id="red">
OFF
</span>

</div>


<div class="status">

<span>HX711</span>

<span id="hx711">
CHECKING
</span>

</div>

</div>


<!-- ================================================= -->
<!-- CURRENT TRANSACTION -->
<!-- ================================================= -->

<div class="card">

<h2>🚚 Current Transaction</h2>


<div class="grid">


<div>

<div class="small">
User
</div>

<div
class="value"
id="currentUser">

-

</div>

</div>


<div>

<div class="small">
Item
</div>

<div
class="value"
id="currentItem">

-

</div>

</div>


<div>

<div class="small">
Target
</div>

<div
class="value"
id="targetWeight">

0 g

</div>

</div>


<div>

<div class="small">
Current Weight
</div>

<div
class="value">

<span id="weight">
0
</span> g

</div>

</div>


</div>


<div class="status">

<span>Status</span>

<span
id="transactionStatus"
class="warning">

IDLE

</span>

</div>


<div class="status">

<span>Last Result</span>

<span
id="lastResult">

-

</span>

</div>


</div>


<!-- ================================================= -->
<!-- ENTITLEMENT -->
<!-- ================================================= -->

<div class="card">

<h2>📦 User Entitlement</h2>


<div class="grid">


<div>

<div class="small">
Rice Remaining
</div>

<div class="value">

<span id="rice">
0
</span> g

</div>

</div>


<div>

<div class="small">
Sugar Remaining
</div>

<div class="value">

<span id="sugar">
0
</span> g

</div>

</div>


</div>

</div>


<!-- ================================================= -->
<!-- TRANSACTION HISTORY -->
<!-- ================================================= -->

<div class="card">

<h2>📋 Transaction History</h2>

<div class="history">

<table>

<thead>

<tr>

<th>User</th>
<th>Item</th>
<th>Weight</th>
<th>Status</th>

</tr>

</thead>


<tbody id="history">

<tr>

<td colspan="4">
No transactions yet
</td>

</tr>

</tbody>

</table>

</div>

</div>


<div class="card">

<div class="small">
Dashboard automatically updates every 1 second.
</div>

</div>


</div>


<script>


// =====================================================
// UPDATE ADMIN DASHBOARD
// =====================================================

function updateDashboard()
{
  fetch("/api/admin-state")

  .then(response => response.json())

  .then(data =>
  {

    // ---------------- WIFI ----------------

    document
      .getElementById("wifi")
      .innerText =
      data.wifi ? "ONLINE" : "OFFLINE";


    // ---------------- SYSTEM ----------------

    let system =
      document.getElementById("system");


    if(data.dispensing)
    {
      system.innerText =
        "DISPENSING";

      system.className =
        "warning";
    }
    else if(data.lastSuccess)
    {
      system.innerText =
        "READY";

      system.className =
        "good";
    }
    else
    {
      system.innerText =
        "ERROR";

      system.className =
        "bad";
    }


    // ---------------- IR ----------------

    let ir =
      document.getElementById("ir");


    if(data.irDetected)
    {
      ir.innerText =
        "OBJECT DETECTED";

      ir.className =
        "good";
    }
    else
    {
      ir.innerText =
        "CLEAR";

      ir.className =
        "warning";
    }


    // ---------------- GREEN LED ----------------

    let green =
      document.getElementById("green");


    if(data.greenLED)
    {
      green.innerText =
        "ON";

      green.className =
        "good";
    }
    else
    {
      green.innerText =
        "OFF";

      green.className =
        "warning";
    }


    // ---------------- RED LED ----------------

    let red =
      document.getElementById("red");


    if(data.redLED)
    {
      red.innerText =
        "ON";

      red.className =
        "bad";
    }
    else
    {
      red.innerText =
        "OFF";

      red.className =
        "warning";
    }


    // ---------------- HX711 ----------------

    let hx =
      document.getElementById("hx711");


    if(data.demoMode)
    {
      hx.innerText =
        "DEMO READY";

      hx.className =
        "warning";
    }
    else if(data.hxReady)
    {
      hx.innerText =
        "READY";

      hx.className =
        "good";
    }
    else
    {
      hx.innerText =
        "NOT READY";

      hx.className =
        "bad";
    }


    // ---------------- CURRENT USER ----------------

    document
      .getElementById("currentUser")
      .innerText =
      data.currentUser;


    // ---------------- CURRENT ITEM ----------------

    document
      .getElementById("currentItem")
      .innerText =
      data.currentItem;

    document
      .getElementById("targetWeight")
      .innerText =
      Math.round(data.target) + " g";


    // ---------------- WEIGHT ----------------

    document
      .getElementById("weight")
      .innerText =
      Math.round(data.weight);


    // ---------------- TRANSACTION STATUS ----------------

    let status =
      document.getElementById(
        "transactionStatus"
      );


    status.innerText =
      data.transactionStatus;


    if(data.dispensing)
    {
      status.className =
        "warning";
    }
    else if(data.lastSuccess)
    {
      status.className =
        "good";
    }
    else
    {
      status.className =
        "bad";
    }


    // ---------------- LAST RESULT ----------------

    document
      .getElementById("lastResult")
      .innerText =
      data.lastMessage;


    // ---------------- ENTITLEMENT ----------------

    document
      .getElementById("rice")
      .innerText =
      Math.round(data.rice);


    document
      .getElementById("sugar")
      .innerText =
      Math.round(data.sugar);


    // ---------------- HISTORY ----------------

    let historyHTML = "";

    if(data.history.length == 0)
    {
      historyHTML =
        "<tr><td colspan='4'>No transactions yet</td></tr>";
    }
    else
    {
      data.history.forEach(function(t)
      {
        historyHTML += "<tr>";

        historyHTML +=
          "<td>" + t.user + "</td>";

        historyHTML +=
          "<td>" + t.item + "</td>";

        historyHTML +=
          "<td>" +
          Math.round(t.weight) +
          " g</td>";

        if(t.success)
        {
          historyHTML +=
            "<td class='success'>SUCCESS</td>";
        }
        else
        {
          historyHTML +=
            "<td class='failed'>FAILED</td>";
        }

        historyHTML += "</tr>";
      });
    }


    document
      .getElementById("history")
      .innerHTML =
      historyHTML;

  })

  .catch(error =>
  {
    document
      .getElementById("wifi")
      .innerText =
      "CONNECTION ERROR";

    document
      .getElementById("wifi")
      .className =
      "bad";
  });
}


// =====================================================
// UPDATE EVERY SECOND
// =====================================================

setInterval(
  updateDashboard,
  1000
);

updateDashboard();

</script>


</body>

</html>
)rawliteral";

  return html;
}


// ============================================================
// CUSTOMER ROOT
// ============================================================

void handleCustomerPage()
{
  server.send(
    200,
    "text/html; charset=utf-8",
    customerPage()
  );
}


// ============================================================
// ADMIN PAGE
// ============================================================

void handleAdminPage()
{
  server.send(
    200,
    "text/html; charset=utf-8",
    adminPage()
  );
}


// ============================================================
// LOGIN API
// ============================================================

void handleLogin()
{
  if (!server.hasArg("pin"))
  {
    server.send(
      400,
      "application/json",
      "{\"success\":false,\"message\":\"PIN missing\"}"
    );

    return;
  }


  String enteredPIN =
    server.arg("pin");


  currentUser = -1;


  for (int i = 0; i < 3; i++)
  {
    if (enteredPIN == users[i].pin)
    {
      currentUser = i;
      break;
    }
  }


  if (currentUser >= 0)
  {
    setGreenLED(true);
    setRedLED(false);


    String response = "{";

    response += "\"success\":true,";

    response +=
      "\"name\":\"" +
      users[currentUser].name +
      "\",";

    response +=
      "\"rice\":" +
      String(users[currentUser].riceEntitlement) +
      ",";

    response +=
      "\"sugar\":" +
      String(users[currentUser].sugarEntitlement);

    response += "}";


    server.send(
      200,
      "application/json",
      response
    );


    Serial.print("Login successful: ");

    Serial.println(
      users[currentUser].name
    );
  }
  else
  {
    setGreenLED(false);

    errorBlink();


    server.send(
      200,
      "application/json",
      "{\"success\":false,\"message\":\"Invalid PIN\"}"
    );


    Serial.println(
      "Invalid PIN entered."
    );
  }
}


// ============================================================
// CUSTOMER STATE API
// ============================================================

void handleState()
{
  if (currentUser < 0)
  {
    server.send(
      200,
      "application/json",
      "{\"loggedIn\":false}"
    );

    return;
  }


  String response = "{";


  response +=
    "\"loggedIn\":true,";


  response +=
    "\"name\":\"" +
    users[currentUser].name +
    "\",";


  response +=
    "\"rice\":" +
    String(users[currentUser].riceEntitlement) +
    ",";


  response +=
    "\"sugar\":" +
    String(users[currentUser].sugarEntitlement);


  response += "}";


  server.send(
    200,
    "application/json",
    response
  );
}


// ============================================================
// START DISPENSING API
// ============================================================

void handleDispense()
{
  if (currentUser < 0)
  {
    server.send(
      200,
      "application/json",
      "{\"success\":false,\"message\":\"Please login first\"}"
    );

    return;
  }


  if (dispensing)
  {
    server.send(
      200,
      "application/json",
      "{\"success\":false,\"message\":\"Dispensing already in progress\"}"
    );

    return;
  }


  if (!server.hasArg("item"))
  {
    server.send(
      400,
      "application/json",
      "{\"success\":false,\"message\":\"Item missing\"}"
    );

    return;
  }


  if (!server.hasArg("amount"))
  {
    server.send(
      400,
      "application/json",
      "{\"success\":false,\"message\":\"Amount missing\"}"
    );

    return;
  }


  String item =
    server.arg("item");

  float amount =
    server.arg("amount").toFloat();


  if (amount <= 0)
  {
    server.send(
      200,
      "application/json",
      "{\"success\":false,\"message\":\"Enter a valid amount\"}"
    );

    return;
  }


  // ========================================================
  // CHECK ENTITLEMENT
  // ========================================================

  if (item == "rice")
  {
    if (users[currentUser].riceEntitlement < amount)
    {
      setGreenLED(false);
      setRedLED(true);

      lastDispenseSuccess = false;

      lastTransactionMessage =
        "Insufficient rice entitlement";

      server.send(
        200,
        "application/json",
        "{\"success\":false,\"message\":\"Insufficient rice entitlement\"}"
      );

      return;
    }
  }
  else if (item == "sugar")
  {
    if (users[currentUser].sugarEntitlement < amount)
    {
      setGreenLED(false);
      setRedLED(true);

      lastDispenseSuccess = false;

      lastTransactionMessage =
        "Insufficient sugar entitlement";

      server.send(
        200,
        "application/json",
        "{\"success\":false,\"message\":\"Insufficient sugar entitlement\"}"
      );

      return;
    }
  }
  else
  {
    server.send(
      400,
      "application/json",
      "{\"success\":false,\"message\":\"Invalid item\"}"
    );

    return;
  }


  // ========================================================
  // PREPARE DEMO DISPENSING
  // ========================================================

  selectedItem = item;
  requestedWeight = amount;

  closeAllServos();

  delay(300);

  currentWeight = 0.0;

  dispensing = true;

  dispenseStartTime = millis();


  lastDispenseSuccess = false;

  lastTransactionMessage =
    "Demo dispensing in progress";


  lastTransactionUser =
    users[currentUser].name;


  lastTransactionItem =
    selectedItem;

  lastTransactionWeight = 0.0;


  // LEDs OFF during dispensing
  allLEDsOff();


  // ========================================================
  // OPEN SELECTED SERVO
  // ========================================================

  if (selectedItem == "rice")
  {
    openRiceServo();

    Serial.println(
      "Rice servo OPEN - DEMO MODE"
    );
  }
  else
  {
    openSugarServo();

    Serial.println(
      "Sugar servo OPEN - DEMO MODE"
    );
  }


  Serial.println(
    "Demo dispensing started."
  );


  server.send(
    200,
    "application/json",
    "{\"success\":true,\"message\":\"Demo dispensing started\"}"
  );
}


// ============================================================
// DISPENSING STATUS API
// ============================================================

void handleDispenseStatus()
{
  String response = "{";


  response +=
    "\"finished\":" +
    String(dispensing ? "false" : "true") +
    ",";


  response +=
    "\"success\":" +
    String(
      lastDispenseSuccess
      ? "true"
      : "false"
    ) +
    ",";


  response +=
    "\"weight\":" +
    String(currentWeight) +
    ",";

  response +=
    "\"target\":" +
    String(requestedWeight) +
    ",";


  response +=
    "\"message\":\"" +
    lastTransactionMessage +
    "\"";


  response += "}";


  server.send(
    200,
    "application/json",
    response
  );
}


// ============================================================
// ADMIN STATE API
// ============================================================

void handleAdminState()
{
  bool hxReady = false;


  String response = "{";


  // WiFi
  response +=
    "\"wifi\":true,";


  // Dispensing
  response +=
    "\"dispensing\":" +
    String(
      dispensing
      ? "true"
      : "false"
    ) +
    ",";


  // IR
  response +=
    "\"irDetected\":" +
    String(
      isIRDetected()
      ? "true"
      : "false"
    ) +
    ",";


  // LEDs
  response +=
    "\"greenLED\":" +
    String(
      digitalRead(GREEN_LED_PIN)
      ? "true"
      : "false"
    ) +
    ",";


  response +=
    "\"redLED\":" +
    String(
      digitalRead(RED_LED_PIN)
      ? "true"
      : "false"
    ) +
    ",";


  // HX711 / DEMO MODE
  response +=
    "\"hxReady\":" +
    String(
      hxReady
      ? "true"
      : "false"
    ) +
    ",";

  response +=
    "\"demoMode\":true,";

  response +=
    "\"target\":" +
    String(requestedWeight) +
    ",";


  // Current user
  if (currentUser >= 0)
  {
    response +=
      "\"currentUser\":\"" +
      users[currentUser].name +
      "\",";
  }
  else
  {
    response +=
      "\"currentUser\":\"-\",";
  }


  // Current item
  if (dispensing)
  {
    response +=
      "\"currentItem\":\"" +
      selectedItem +
      "\",";
  }
  else
  {
    response +=
      "\"currentItem\":\"" +
      lastTransactionItem +
      "\",";
  }


  // Weight
  response +=
    "\"weight\":" +
    String(currentWeight) +
    ",";


  // Transaction status
  String transactionStatus;


  if (dispensing)
  {
    transactionStatus =
      "DISPENSING";
  }
  else if (!lastDispenseSuccess &&
           lastTransactionMessage != "System ready")
  {
    transactionStatus =
      "ERROR";
  }
  else
  {
    transactionStatus =
      "IDLE";
  }


  response +=
    "\"transactionStatus\":\"" +
    transactionStatus +
    "\",";


  // Last result
  response +=
    "\"lastSuccess\":" +
    String(
      lastDispenseSuccess
      ? "true"
      : "false"
    ) +
    ",";


  response +=
    "\"lastMessage\":\"" +
    lastTransactionMessage +
    "\",";


  // Entitlement
  if (currentUser >= 0)
  {
    response +=
      "\"rice\":" +
      String(
        users[currentUser].riceEntitlement
      ) +
      ",";


    response +=
      "\"sugar\":" +
      String(
        users[currentUser].sugarEntitlement
      ) +
      ",";
  }
  else
  {
    response +=
      "\"rice\":0,";

    response +=
      "\"sugar\":0,";
  }


  // History
  response += "\"history\":[";


  for (int i = 0; i < historyCount; i++)
  {
    if (i > 0)
    {
      response += ",";
    }


    response += "{";


    response +=
      "\"user\":\"" +
      historyList[i].user +
      "\",";


    response +=
      "\"item\":\"" +
      historyList[i].item +
      "\",";


    response +=
      "\"weight\":" +
      String(
        historyList[i].weight
      ) +
      ",";


    response +=
      "\"success\":" +
      String(
        historyList[i].success
        ? "true"
        : "false"
      );


    response += "}";
  }


  response += "]";


  response += "}";


  server.send(
    200,
    "application/json",
    response
  );
}


// ============================================================
// LOGOUT
// ============================================================

void handleLogout()
{
  // Do not allow logout while dispensing
  if (dispensing)
  {
    server.send(
      200,
      "application/json",
      "{\"success\":false,\"message\":\"Cannot logout during dispensing\"}"
    );

    return;
  }


  currentUser = -1;

  selectedItem = "";

  setGreenLED(true);
  setRedLED(false);


  server.send(
    200,
    "application/json",
    "{\"success\":true}"
  );
}


// ============================================================
// DISPENSING CONTROL
// ============================================================

void updateDispensing()
{
  if (!dispensing)
  {
    return;
  }


  // ========================================================
  // DEMO DISPENSING TIMER
  // ========================================================

  unsigned long elapsed =
    millis() - dispenseStartTime;


  if (elapsed < DEMO_DISPENSE_TIME)
  {
    currentWeight =
      requestedWeight *
      ((float)elapsed / (float)DEMO_DISPENSE_TIME);

    lastTransactionWeight = currentWeight;

    return;
  }


  currentWeight = requestedWeight;
  lastTransactionWeight = requestedWeight;

  closeAllServos();

  delay(300);


  // ======================================================
  // DEDUCT ENTITLEMENT
  // ======================================================

  if (currentUser >= 0)
  {
    if (selectedItem == "rice")
    {
      users[currentUser].riceEntitlement
        -= requestedWeight;

      if (users[currentUser].riceEntitlement < 0)
      {
        users[currentUser].riceEntitlement = 0;
      }
    }

    if (selectedItem == "sugar")
    {
      users[currentUser].sugarEntitlement
        -= requestedWeight;

      if (users[currentUser].sugarEntitlement < 0)
      {
        users[currentUser].sugarEntitlement = 0;
      }
    }
  }


  // ======================================================
  // TRANSACTION SUCCESS
  // ======================================================

  dispensing = false;

  lastDispenseSuccess = true;

  lastTransactionMessage =
    "Demo dispensing completed";


  addTransaction(
    lastTransactionUser,
    lastTransactionItem,
    requestedWeight,
    true
  );


  // Green = success
  setGreenLED(true);
  setRedLED(false);


  Serial.println(
    "================================"
  );

  Serial.println(
    "DEMO DISPENSING COMPLETE"
  );

  Serial.print(
    "Item: "
  );

  Serial.println(
    selectedItem
  );

  Serial.print(
    "Requested amount: "
  );

  Serial.print(
    requestedWeight
  );

  Serial.println(
    " g"
  );

  Serial.println(
    "================================"
  );
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);

  delay(1000);


  Serial.println();
  Serial.println(
    "========================================"
  );

  Serial.println(
    "       SMART RATION DISPENSER"
  );

  Serial.println(
    "========================================"
  );


  // ========================================================
  // GPIO
  // ========================================================

  pinMode(
    GREEN_LED_PIN,
    OUTPUT
  );

  pinMode(
    RED_LED_PIN,
    OUTPUT
  );

  pinMode(
    IR_SENSOR_PIN,
    INPUT_PULLUP
  );


  allLEDsOff();


  // ========================================================
  // SERVO
  // ========================================================

  riceServo.setPeriodHertz(50);

  sugarServo.setPeriodHertz(50);


  riceServo.attach(
    RICE_SERVO_PIN,
    500,
    2400
  );


  sugarServo.attach(
    SUGAR_SERVO_PIN,
    500,
    2400
  );


  closeAllServos();


  Serial.println(
    "Servos initialized."
  );


  // ========================================================
  // HX711
  // ========================================================

  scale.begin(HX711_DOUT_PIN, HX711_SCK_PIN, false, false);


  scale.set_scale(
    calibration_factor
  );


  Serial.println(
    "HX711 initialized. DEMO MODE: weight feedback bypassed."
  );


  // ========================================================
  // WIFI ACCESS POINT
  // ========================================================

  WiFi.mode(
    WIFI_AP
  );


  bool apStarted =
    WiFi.softAP(
      WIFI_SSID,
      WIFI_PASSWORD
    );


  if (apStarted)
  {
    Serial.println();

    Serial.println(
      "WiFi Access Point started."
    );

    Serial.print(
      "WiFi Name: "
    );

    Serial.println(
      WIFI_SSID
    );

    Serial.print(
      "WiFi Password: "
    );

    Serial.println(
      WIFI_PASSWORD
    );

    Serial.print(
      "ESP32 IP Address: "
    );

    Serial.println(
      WiFi.softAPIP()
    );
  }
  else
  {
    Serial.println(
      "ERROR: WiFi AP failed."
    );
  }


  // ========================================================
  // CUSTOMER SERVER
  // ========================================================

  server.on(
    "/",
    HTTP_GET,
    handleCustomerPage
  );


  // ========================================================
  // ADMIN DASHBOARD
  // ========================================================

  server.on(
    "/dashboard",
    HTTP_GET,
    handleAdminPage
  );


  // ========================================================
  // API ROUTES
  // ========================================================

  server.on(
    "/api/login",
    HTTP_POST,
    handleLogin
  );


  server.on(
    "/api/state",
    HTTP_GET,
    handleState
  );


  server.on(
    "/api/dispense",
    HTTP_POST,
    handleDispense
  );


  server.on(
    "/api/dispense-status",
    HTTP_GET,
    handleDispenseStatus
  );


  server.on(
    "/api/admin-state",
    HTTP_GET,
    handleAdminState
  );


  server.on(
    "/api/logout",
    HTTP_POST,
    handleLogout
  );


  // ========================================================
  // START WEB SERVER
  // ========================================================

  server.begin();


  Serial.println(
    "Web server started."
  );


  // ========================================================
  // INITIAL SYSTEM STATE
  // ========================================================

  lastDispenseSuccess = true;

  lastTransactionMessage =
    "System ready";


  setGreenLED(true);
  setRedLED(false);


  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    "             SYSTEM READY"
  );

  Serial.println(
    "========================================"
  );

  Serial.println(
    "CUSTOMER DISPLAY:"
  );

  Serial.println(
    "http://192.168.4.1/"
  );

  Serial.println();

  Serial.println(
    "ADMIN DASHBOARD:"
  );

  Serial.println(
    "http://192.168.4.1/dashboard"
  );

  Serial.println(
    "========================================"
  );
}


// ============================================================
// LOOP
// ============================================================

void loop()
{
  // VERY IMPORTANT:
  // Processes customer page requests,
  // admin dashboard requests,
  // login requests,
  // dispensing requests, etc.
  server.handleClient();


  // Run the actual dispensing logic
  updateDispensing();


  delay(10);
}
