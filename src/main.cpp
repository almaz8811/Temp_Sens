#include <Arduino.h>
/*
 Name:		Blynk_Template.ino
 Created:	9/19/2019 9:04:46 AM
 Author:	AlMaz
*/

// Подключение библиотек
#include <BlynkSimpleEsp8266.h>
#include <Ticker.h>
#include <EEPROM.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiManager.h>
#include <DHT.h>
#include <TickerScheduler.h> // https://github.com/Toshik/TickerScheduler
#include <GyverButton.h>
#include <SPI.h>
#include <GyverTM1637.h>
#include <PubSubClient.h>

// Параметры
#define NAME_DEVICE "Temp_Sens_Vagon" // Имя устройства
#define DHTPIN 12					  // Назначить пин датчика температуры
#define DHTTYPE DHT22				  // DHT 22, AM2302, AM2321
#define CLK 2						  // Назначить пин дисплея
#define DIO 3						  // Назначить пин дисплея
#define BUTTON_SYS0_PIN 0			  // Назначить кномпку меню и сброса параметров
#define BTN_UP_PIN 5				  // кнопка Up подключена сюда (BTN_PIN --- КНОПКА --- GND)
#define BTN_DOWN_PIN 4				  // кнопка Down подключена сюда (BTN_PIN --- КНОПКА --- GND)
// Настройки MQTT
#define mqtt_server "tailor.cloudmqtt.com"		  // Имя сервера MQTT
#define mqtt_port 11995							  // Порт для подключения к серверу MQTT
#define mqtt_login "xnyqpfbu"					  // Логин от сервер
#define mqtt_password "q94Tbl-0-WxH"			  // Пароль от сервера
#define mqtt_topic_temp "/sensors/dht/vagon/temp" // Топик температуры
#define mqtt_topic_hum "/sensors/dht/vagon/hum"   // Топик влажности

/* User defines ---------------------------------------------------------*/
#define BLYNK_PRINT Serial
#define LED_SYS_PIN 13
#define BUTTON_SYS_B0_VPIN V20
#define WIFI_SIGNAL_VPIN V80 // Пин уровня сигнала WiFi
#define INTERVAL_PRESSED_RESET_ESP 3000L
#define INTERVAL_PRESSED_RESET_SETTINGS 5000L
#define INTERVAL_PRESSED_SHORT 50
#define INTERVAL_SEND_DATA 30033L
#define INTERVAL_RECONNECT 60407L
#define INTERVAL_REFRESH_DATA 4065L
#define WIFI_MANAGER_TIMEOUT 180
#define EEPROM_SETTINGS_SIZE 512
#define EEPROM_START_SETTING_WM 0
#define EEPROM_SALT_WM 12661
#define LED_SYS_TOGGLE() digitalWrite(LED_SYS_PIN, !digitalRead(LED_SYS_PIN))
#define LED_SYS_ON() digitalWrite(LED_SYS_PIN, LOW)
#define LED_SYS_OFF() digitalWrite(LED_SYS_PIN, HIGH)
/* CODE END UD */

GButton butt_Up(BTN_UP_PIN);	 //Объявляем кнопку Up
GButton butt_Down(BTN_DOWN_PIN); //Объявляем кнопку Dovn
DHT dht(DHTPIN, DHTTYPE);		 //Объявляем датчик температуры
GyverTM1637 disp(CLK, DIO);		 //Объявляем дисплей
WiFiClient espClient;
PubSubClient client(espClient);

long lastMsg = 0; // Последнее сообщение MQTT

/* CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/
bool shouldSaveConfigWM = false; //flag for saving data
bool btnSystemState0 = false;
bool triggerBlynkConnect = false;
bool isFirstConnect = true; // Keep this flag not to re-sync on every reconnection

int startPressBtn = 0;

//structure for initial settings. It now takes 116 bytes
typedef struct
{
	char host[33] = NAME_DEVICE;			  // 33 + '\0' = 34 bytes
	char blynkToken[33] = "";				  // 33 + '\0' = 34 bytes
	char blynkServer[33] = "blynk-cloud.com"; // 33 + '\0' = 34 bytes
	char blynkPort[6] = "8442";				  // 04 + '\0' = 05 bytes
	int salt = EEPROM_SALT_WM;				  // 04		 = 04 bytes
} WMSettings;								  // 111 + 1	 = 112 bytes (112 this is a score of 0)
//-----------------------------------------------------------------------------------------

WMSettings wmSettings;
BlynkTimer timer;
Ticker tickerESP8266;

//Планировщик задач (Число задач)
TickerScheduler ts(2);

//Declaration OTA WebUpdater
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

/* CODE BEGIN PFP */
/* Private function prototypes -----------------------------------------------*/
static void configModeCallback(WiFiManager *myWiFiManager);
static void saveConfigCallback(void);
static void tick(void);
static void untick(void);
static void readSystemKey(void);
static void timerRefreshData(void);
static void timerSendServer(void);
static void timerReconnect(void);
/* CODE END PFP */

// Запрос данных с MQTT
/* void callback(char *topic, byte *payload, unsigned int length)
{
	Serial.print("Message arrived [");
	Serial.print(topic); // отправляем в монитор порта название топика
	Serial.print("] ");
	for (int i = 0; i < length; i++)
	{ // отправляем данные из топика
		Serial.print((char)payload[i]);
	}
	Serial.println();
} */

void reconnect()
{
	while (!client.connected())
	{ // крутимся пока не подключемся.
		// создаем случайный идентификатор клиента
		String clientId = "ESP8266Client-";
		clientId += String(random(0xffff), HEX);
		// подключаемся, в client.connect передаем ID, логин и пасс
		if (client.connect(clientId.c_str(), mqtt_login, mqtt_password))
		{
			client.subscribe(mqtt_topic_temp); // подписываемся на топик, в который же пишем данные
			client.subscribe(mqtt_topic_hum);  // подписываемся на топик, в который же пишем данные
		}
		else
		{
			// иначе ругаемся в монитор порта
		}
	}
}

void DHT_init()
{
	dht.begin();		   //Запускаем датчик
	delay(1000);		   // Нужно ждать иначе датчик не определится правильно
	dht.readTemperature(); // обязательно делаем пустое чтение первый раз иначе чтение статуса не сработает
	ts.add(0, 5000, [&](void *) { // Запустим задачу 0 с интервалом
		float t = dht.readTemperature();
		float h = dht.readHumidity();
		Blynk.virtualWrite(V1, t);
		Blynk.virtualWrite(V2, h);
		disp.displayInt(round(t * 10) / 10); // Убираем дробную часть
		disp.displayByte(0, _C); // Вывод символа C
	},
		   nullptr, true);
	ts.add(1, 8000, [&](void *) { // Запустим задачу 1 с интервалом
		char msgT[10];
		char msgH[10];
		float tm = dht.readTemperature();
		float hm = dht.readHumidity();
		dtostrf(tm, 5, 1, msgT);
		client.publish(mqtt_topic_temp, msgT); // пишем в топик
		dtostrf(hm, 5, 1, msgH);
		client.publish(mqtt_topic_hum, msgH); // пишем в топик
	},
		   nullptr, true);
}

void setup()
{
	//Serial.begin(115200);
	pinMode(BUTTON_SYS0_PIN, INPUT_PULLUP);
	pinMode(LED_SYS_PIN, OUTPUT);

	// Read the WM settings data from EEPROM to RAM
	EEPROM.begin(EEPROM_SETTINGS_SIZE);
	EEPROM.get(EEPROM_START_SETTING_WM, wmSettings);
	EEPROM.end();

	if (wmSettings.salt != EEPROM_SALT_WM)
	{
		//Serial.println(F("Invalid wmSettings in EEPROM, trying with defaults"));
		WMSettings defaults;
		wmSettings = defaults;
	}

	tickerESP8266.attach(0.5, tick); // start ticker with 0.5 because we start in AP mode and try to connect

	WiFiManager wifiManager;
	wifiManager.setConfigPortalTimeout(WIFI_MANAGER_TIMEOUT);
	WiFiManagerParameter custom_device_name_text("<br/>Enter name of the device<br/>or leave it as it is<br/>");
	wifiManager.addParameter(&custom_device_name_text);
	WiFiManagerParameter custom_device_name("device-name", "device name", wmSettings.host, 33);
	wifiManager.addParameter(&custom_device_name);
	WiFiManagerParameter custom_blynk_text("<br/>Blynk config.<br/>");
	wifiManager.addParameter(&custom_blynk_text);
	WiFiManagerParameter custom_blynk_token("blynk-token", "blynk token", wmSettings.blynkToken, 33);
	wifiManager.addParameter(&custom_blynk_token);
	WiFiManagerParameter custom_blynk_server("blynk-server", "blynk server", wmSettings.blynkServer, 33);
	wifiManager.addParameter(&custom_blynk_server);
	WiFiManagerParameter custom_blynk_port("blynk-port", "port", wmSettings.blynkPort, 6);
	wifiManager.addParameter(&custom_blynk_port);
	wifiManager.setSaveConfigCallback(saveConfigCallback);
	wifiManager.setAPCallback(configModeCallback);

	if (wifiManager.autoConnect(wmSettings.host))
	{
		//Serial.println(F("Connected WiFi!"));
	}
	else
	{
		//Serial.println(F("failed to connect and hit timeout"));
	}

	untick(); // cancel the flashing LED

	// Copy the entered values to the structure
	strcpy(wmSettings.host, custom_device_name.getValue());
	strcpy(wmSettings.blynkToken, custom_blynk_token.getValue());
	strcpy(wmSettings.blynkServer, custom_blynk_server.getValue());
	strcpy(wmSettings.blynkPort, custom_blynk_port.getValue());

	if (shouldSaveConfigWM)
	{
		LED_SYS_ON();
		// Записать данные в EEPROM
		EEPROM.begin(EEPROM_SETTINGS_SIZE);
		EEPROM.put(EEPROM_START_SETTING_WM, wmSettings);
		EEPROM.end();
		LED_SYS_OFF();
	}

	// Запуск OTA WebUpdater
	MDNS.begin(wmSettings.host);
	httpUpdater.setup(&httpServer);
	httpServer.begin();
	MDNS.addService("http", "tcp", 80);
	//Serial.printf("HTTPUpdateServer ready! Open http://%s.local/update in your browser\n", wmSettings.host);

	// Настройка подключения к серверу Blynk
	Blynk.config(wmSettings.blynkToken, wmSettings.blynkServer, atoi(wmSettings.blynkPort));

	if (Blynk.connect())
	{
		//TODO: something to do if connected
	}
	else
	{
		//TODO: something to do if you failed to connect
	}

	timer.setInterval(INTERVAL_REFRESH_DATA, timerRefreshData);
	timer.setInterval(INTERVAL_SEND_DATA, timerSendServer);
	timer.setInterval(INTERVAL_RECONNECT, timerReconnect);
	DHT_init();
	disp.clear();
	disp.brightness(7); // яркость, 0 - 7 (минимум - максимум)
	disp.point(0); // Отключить точки на дисплее
	client.setServer(mqtt_server, mqtt_port); // указываем адрес брокера и порт
	//client.setCallback(callback);			  // указываем функцию которая вызывается когда приходят данные от брокера
}

void loop()
{
	if (Blynk.connected())
	{
		Blynk.run(); // Инициализация сервера Blynk
	}
	else
	{
		if (!tickerESP8266.active())
		{
			tickerESP8266.attach(2, tick);
		}
	}

	timer.run(); // Инициализация BlynkTimer
	httpServer.handleClient(); // Инициализация OTA WebUpdater
	readSystemKey();
	ts.update(); //планировщик задач

	butt_Up.tick(); // обязательная функция отработки. Должна постоянно опрашиватьсяb
	if (butt_Up.isSingle()) Serial.println("Single");     // проверка на один клик

	if (!client.connected())
	{				 // проверяем подключение к брокеру
		reconnect(); // еще бы проверить подкючение к wifi...
	}
	client.loop();
}

/* BLYNK CODE BEGIN */
BLYNK_CONNECTED()
{
	untick();
	// Serial.println(F("Blynk Connected!"));
	// Serial.println(F("local ip"));
	// Serial.println(WiFi.localIP());
	char str[32];
	sprintf_P(str, PSTR("%s Online!"), wmSettings.host);
	Blynk.notify(str);
	if (isFirstConnect)
	{
		Blynk.syncAll();
		isFirstConnect = false;
	}
}

BLYNK_WRITE(BUTTON_SYS_B0_VPIN) // Example
{
	//TODO: something to do when a button is clicked in the Blynk app

	//Serial.println(F("System_0 button pressed is App!"));
}
/* BLYNK CODE END */

/* CODE BEGIN USER FUNCTION */
static void timerRefreshData(void)
{
	//TODO: here are functions for updating data from sensors, ADC, etc ...
}

static void timerSendServer(void)
{
	if (Blynk.connected())
	{
		//TODO: here are the functions that send data to the Blynk server
		Blynk.virtualWrite(WIFI_SIGNAL_VPIN, map(WiFi.RSSI(), -105, -40, 0, 100)); // Получаем уровень сигнала Wifi
	}
	else
	{
		//TODO:
	}
}

static void timerReconnect(void)
{
	if (WiFi.status() != WL_CONNECTED)
	{
/* 		Serial.println(F("WiFi not connected"));
		if (WiFi.begin() == WL_CONNECTED)
		{
			Serial.println(F("WiFi reconnected"));
		}
		else
		{
			Serial.println(F("WiFi not reconnected"));
		} */
	}
	else // if (WiFi.status() == WL_CONNECTED)
	{
/* 		Serial.println(F("WiFi in connected"));

		if (!Blynk.connected())
		{
			if (Blynk.connect())
			{
				Serial.println(F("Blynk reconnected"));
			}
			else
			{
				Serial.println(F("Blynk not reconnected"));
			}
		}
		else
		{
			Serial.println(F("Blynk in connected"));
		} */
	}
}

static void configModeCallback(WiFiManager *myWiFiManager)
{
/* 	Serial.println(F("Entered config mode"));
	Serial.println(WiFi.softAPIP());
	//if you used auto generated SSID, print it
	Serial.println(myWiFiManager->getConfigPortalSSID());
	//entered config mode, make led toggle faster */
	tickerESP8266.attach(0.2, tick);
}

//callback notifying us of the need to save config
static void saveConfigCallback()
{
	//Serial.println(F("Should save config"));
	shouldSaveConfigWM = true;
}

static void tick(void)
{
	//toggle state
	LED_SYS_TOGGLE(); // set pin to the opposite state
}

static void untick(void)
{
	tickerESP8266.detach();
	LED_SYS_OFF(); //keep LED off
}

static void readSystemKey(void)
{
	if (!digitalRead(BUTTON_SYS0_PIN) && !btnSystemState0)
	{
		btnSystemState0 = true;
		startPressBtn = millis();
	}
	else if (digitalRead(BUTTON_SYS0_PIN) && btnSystemState0)
	{
		btnSystemState0 = false;
		int pressTime = millis() - startPressBtn;

		if (pressTime > INTERVAL_PRESSED_RESET_ESP && pressTime < INTERVAL_PRESSED_RESET_SETTINGS)
		{
			if (Blynk.connected())
			{
				Blynk.notify(String(wmSettings.host) + F(" reboot!"));
			}
			Blynk.disconnect();
			tickerESP8266.attach(0.1, tick);
			delay(2000);
			ESP.restart();
		}
		else if (pressTime > INTERVAL_PRESSED_RESET_SETTINGS)
		{
			if (Blynk.connected())
			{
				Blynk.notify(String(wmSettings.host) + F(" setting reset! Connected WiFi AP this device!"));
			}
			disp.displayByte(_r, _E, _S, _t);
			WMSettings defaults;
			wmSettings = defaults;

			LED_SYS_ON();
			// We write the default data to EEPROM
			EEPROM.begin(EEPROM_SETTINGS_SIZE);
			EEPROM.put(EEPROM_START_SETTING_WM, wmSettings);
			EEPROM.end();
			//------------------------------------------
			LED_SYS_OFF();

			delay(1000);
			WiFi.disconnect();
			delay(1000);
			ESP.restart();
		}
		else if (pressTime < INTERVAL_PRESSED_RESET_ESP && pressTime > INTERVAL_PRESSED_SHORT)
		{
			//Serial.println(F("System button_0 pressed is Device!"));
			// TODO: insert here what will happen when you press the ON / OFF button
		}
		else if (pressTime < INTERVAL_PRESSED_SHORT)
		{
			// Serial.printf("Fixed false triggering %ims", pressTime);
			// Serial.println();
		}
	}
}
/* CODE END USER FUNCTION */
