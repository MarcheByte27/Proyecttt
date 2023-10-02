#include "main.h"

void setup()
{
  // PIXELES APAGADOS DE PRIMERAS
  encenderLed(0, 0, 0, 0);

  Serial.begin(115200);

  SPIFFS.begin(true);
  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED))
  {
#ifdef MY_DEBUG
    Serial.println("SPIFFS Mount Failed");
#endif
    esp_restart();
  }

  listDir(SPIFFS, "/", 0);
  //------ Borrar cuando se acabe de modificar la página web -----

  deleteFile(SPIFFS, "/WebServer.html");
  deleteFile(SPIFFS, "/NoModif.html");
  deleteFile(SPIFFS, "/PASS.txt");

  //-- Se cargan en la parte de inicializar variables

  InicializarVariables();
  initServer();

  handle_Wifikeepalive = xTimerCreate("reconect wifi", (timeReconnectFull * 1000), pdTRUE, NULL, TimerReconect_wifi);
  xTimerStart(handle_Wifikeepalive, 10);

  myStepper.setSpeed(30);

  // Cerrar en caso de ambigüedad
  potenStatus = analogRead(pinPoten);
  if (potenStatus >= 3800)
    Serial.println("Puerta cerrada");
  else if (potenStatus <= 200)
    Serial.println("Puerta abierta");
  else
  {
    {
      encenderLed(0, 0, 255, 300); // azul
      Serial.println("Cerrando...");
      for (;;)
      {
        if (analogRead(pinPoten) >= 4000)
          break;
        vueltas(-10);
      }
    }
    Serial.println("Puerta cerrada correctamente");
  }
  encenderLed(0, 0, 0, 0);

  pinMode(13, INPUT_PULLUP);
  attachInterrupt(13, buttonFunction, FALLING);

  xTaskCreate(
      TaskLeerNFC, "TaskLeerNFC",
      8192,
      NULL, 4,
      &xLeerNFC);

  // se enciende azul para decir que está correcto
  encenderLed(0, 0, 255, 1000);
  encenderLed(0, 0, 0, 0);
}

void loop() {}

// TAREAS
void TaskLeerNFC(void *pvParameters)
{
  nfc.begin();                                     // Inicializa el modulo
  uint32_t versiondata = nfc.getFirmwareVersion(); // Obten la version del módulo
  if (!versiondata)
  { // Verifica si se ha encontrado al placa
    Serial.print("Placa PN53x no encontrada lectura nfc");
  }
  Serial.print("Found chip PN5");
  Serial.println((versiondata >> 24) & 0xFF, HEX);
  Serial.print("Firmware ver. ");
  Serial.print((versiondata >> 16) & 0xFF, DEC);
  Serial.print('.');
  Serial.println((versiondata >> 8) & 0xFF, DEC);
  nfc.SAMConfig(); // Configura el módulo RFID para la lectura
  Serial.println("Esperando tarjeta ISO14443A ...");

  for (;;)
  {
    uint8_t success;                       // Controle de sucesso
    uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0}; // Buffer UID
    uint8_t uidLength;                     // Tamanho da UID (4 ou 7 bytes depende de tarjeta)

    success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength); // Informacion do NFC

    if (success)
    {
      Serial.println("Found an ISO14443A card");
      Serial.print("  UID Length: ");
      Serial.print(uidLength, DEC);
      Serial.println(" bytes");
      Serial.print("  UID Value: ");
      nfc.PrintHex(uid, uidLength); // Imprime el UID
      Serial.println("");
      if (uidLength == 4 && !estaUidVetado(uid, uidLength))
      { // Verifica que la tarjeta es de 4 bytes y no esté vetada
        Serial.println("LEYENDO...");
        uint8_t fini = 0;
        for (int i = 0; i <= 15; i++)
        {
          if (fini)
            break;
          success = nfc.mifareclassic_AuthenticateBlock(uid, uidLength, (i * 4), 1, keyA); // Verifica claves

          if (success)
          {
            for (int x = 0; x < 3; x++)
            {
              if (fini)
                break;

              if (x == 0 && x == 0)
                continue;

              uint8_t data[16];                                           // Declaracion de un array de tamaño 16
              success = nfc.mifareclassic_ReadDataBlock(i * 4 + x, data); // Lectura bloque x del sector i
              if (success)
              {
                // Verificar si abre puerta o si vacio
                for (int y = 0; y <= 15; y = y + 2)
                {
                  if (fini)
                    break;

                  uint8_t z = data[y];
                  uint8_t p = data[y + 1];

                  if (z == ZONA.toInt() && (p == PUERTA.toInt() || p == 0x00))
                  {
                    Serial.println("LECTURA CORRECTA.");
                    if (botonPulsado)
                    {
                      posibleCerrar = 1;
                      botonPulsado = 0;
                      vTaskSuspend(xLeerNFC);
                    }
                    else
                      AbrirPuerta();
                    fini = 1;
                    break;
                  }
                }
              }
              else
              {
                Serial.print("No es posible realizar la lectura del bloque ");
                Serial.println(i * 4 + x, DEC);
              }
            }
          }
          else
          {
            Serial.print("La clave no es correcta para el sector ");
            Serial.println(i, DEC);
          }
        }

        if (!fini) // No se ha abierto la puerta
        {
          if (botonPulsado == 1)
          {
            vTaskDelete(xButton);
            attachInterrupt(13, buttonFunction, FALLING);
            tareaCreada = 0;
            botonPulsado = 0;
          }
          encenderLed(255, 0, 0, 300);
          encenderLed(0, 0, 0, 300);
          encenderLed(255, 0, 0, 300);
          encenderLed(0, 0, 0, 0);
          Serial.println("Tarjeta no compatible y/o vetada");
        }

        Serial.println("SEPARE LA TARJETA. En 3 segundos podrá leer otra tarjeta");
        vTaskDelay(3000);
        Serial.println("------- \n\n Esperando tarjeta ISO14443A ...");
      }
      else
      {

        encenderLed(255, 0, 0, 300);
        encenderLed(0, 0, 0, 300);
        encenderLed(255, 0, 0, 300);
        encenderLed(0, 0, 0, 0);

        Serial.println("Tarjeta no compatible y/o vetada");
        Serial.println("SEPARE LA TARJETA. En 3 segundos podrá leer otra tarjeta");
        vTaskDelay(3000);
        Serial.println("------- \n\n Esperando tarjeta ISO14443A ...");
      }
    }

    vTaskDelay(500); // Pausamos para que funcionen otras tareas
  }
}

void TaskConnectToServer(void *pvParameters)
{
  vTaskSuspend(xLeerNFC); // Suspendemos tarea de lectura
  detachInterrupt(13);    // Paramos interrupción de botón
  encenderLed(255, 100, 0, 0);

  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(RouterSsid.c_str(), RouterPass.c_str());
  Serial.println("Conectando a servidor...");
  int i = 0;
  while ((i < 10) && (WiFi.status() != WL_CONNECTED))
  {
    delay(500);
    Serial.print(".");
    i++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    if (updateUidsVetados() == 1)
    {
      xTimerChangePeriod(handle_Wifikeepalive, (timeReconnectFull * 1000), 0);
      encenderLed(0, 0, 0, 300);
      encenderLed(0, 255, 0, 300);
      encenderLed(0, 0, 0, 0);
    }
    else
    {
      xTimerChangePeriod(handle_Wifikeepalive, (timeReconnectMid * 1000) / portTICK_PERIOD_MS, 0);
      encenderLed(0, 0, 0, 300);
      encenderLed(255, 255, 0, 300);
      encenderLed(0, 0, 0, 0);
    }
  }
  else
  {
    xTimerChangePeriod(handle_Wifikeepalive, (timeReconnectMid * 1000) / portTICK_PERIOD_MS, 0);
    encenderLed(0, 0, 0, 300);
    encenderLed(255, 255, 0, 300);
    encenderLed(0, 0, 0, 0);
  }
  WiFi.disconnect();
  initServer();

  vTaskResume(xLeerNFC);                        // Activamos tarea de lectura
  attachInterrupt(13, buttonFunction, FALLING); // Activamos interrupcion
  vTaskDelete(xConnectToserver);
  for(;;);
}

void TaskButton(void *pvParameters)
{
  Serial.println("BOTÓN PULSADO");
  detachInterrupt(13);
  potenStatus = analogRead(pinPoten);
  if (posibleCerrar)
  {
    posibleCerrar = 0;
    botonPulsado = 0;
    Serial.println("Cerrar por boton");
    AbrirPuerta();
    vTaskResume(xLeerNFC); // Activamos tarea de lectura
  }
  else if (potenStatus <= 200) // su puerta está abierta
  {
    botonPulsado = 1;
    for (int i = 0; i < 8; i++) // espera algo más de 10 segundos
    {
      encenderLed(0, 0, 255, 700); // parpadea azul lento
      encenderLed(0, 0, 0, 700);
      if (posibleCerrar)
        break;
    }
    if (!posibleCerrar)
    {
      Serial.println("Tiempo de espera acabado");
      encenderLed(255, 0, 0, 700); // parpadea rojo 1 vez
      encenderLed(0, 0, 0, 0);
    }
    else
    {
      Serial.println("Tarjeta leída correctamente");
      encenderLed(0, 255, 0, 700); // parpadea verde 1 vez
      encenderLed(0, 0, 0, 0);
    }
    botonPulsado = 0;
  }
  else
  {
    Serial.println("Puerta cerrada, no se puede realizar dicha acción");
    for (int i = 0; i < 3; i++)
    {
      encenderLed(255, 0, 0, 300); // parpadea rojo
      encenderLed(0, 0, 0, 0);
    }
  }
  attachInterrupt(13, buttonFunction, FALLING);
  tareaCreada = 0;
  vTaskDelete(xButton);
  for (;;)
    ;
}

// Timers e Interrupciones

void IRAM_ATTR buttonFunction()
{
  if (!tareaCreada)
  {
    xTaskCreate(
        TaskButton, "TaskButton",
        4096,
        NULL, 6,
        &xButton);
    tareaCreada = 1;
  }
}

void TimerReconect_wifi(void *arg)
{
  xTaskCreate(
      TaskConnectToServer, "TaskConnectToServer",
      4096,
      NULL, 5,
      &xConnectToserver);
}

// FUNCIONES DE AYUDA

void encenderLed(int R, int G, int B, int time)
{
  pixels.setPixelColor(0, pixels.Color(R, G, B));
  pixels.show();
  if (time != 0)
    vTaskDelay(time);
}

void vueltas(int v)
{
  myStepper.step(v);
  digitalWrite(33, LOW);
  digitalWrite(25, LOW);
  digitalWrite(26, LOW);
  digitalWrite(27, LOW);
}

void AbrirPuerta()
{

  potenStatus = analogRead(pinPoten);
  // abrir
  if (potenStatus >= 3800)
  {
    encenderLed(0, 255, 255, 300); // azul verdoso
    Serial.println("Abriendo...");

    for (;;)
    {
      if (analogRead(pinPoten) <= 100)
        break;
      vueltas(10);
    }
  }
  // cerrar
  else if (potenStatus <= 200)
  {
    encenderLed(0, 0, 255, 300); // azul
    Serial.println("Cerrando...");
    for (;;)
    {
      if (analogRead(pinPoten) >= 4000)
        break;

      vueltas(-10);
    }
  }
  vTaskDelay(500);
  encenderLed(0, 0, 0, 0);
}

void initServer()
{

  Serial.println("Configuring access point...");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(SSID.c_str(), PASSWORD.c_str());
  Serial.println("Conectando...");

  IPAddress myIP = WiFi.softAPIP();
  String h = IP + "/";
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("WebServer.html");
  server.on("/changeSSID", HTTP_POST, procSSID);
  server.on("/location", HTTP_POST, procLocation);
  server.on("/changePass", HTTP_POST, procPass);
  server.on("/changeServer", HTTP_POST, procServer);
  server.on("/changeRouter", HTTP_POST, procRouter);
  server.onNotFound([](AsyncWebServerRequest *request)
                    { request->send(400, "text/plain", "Not found"); });

  IP = myIP.toString();
  server.begin();
  Serial.print("SSID: ");
  Serial.println(SSID);
  Serial.print("IP address: ");
  Serial.println(IP);
}

void InicializarVariables()
{

  // red wifi
  if (!SPIFFS.exists("/SSID.txt"))
  {
    String comando = "ConfigNFC";
    writeFile(SPIFFS, "/SSID.txt", (char *)comando.c_str());
  }
  if (!SPIFFS.exists("/PASS.txt"))
  {
    String comando = "ConfigNFC";
    writeFile(SPIFFS, "/PASS.txt", (char *)comando.c_str());
  }
  if (!SPIFFS.exists("/WebServer.html"))
  {
    writeFile(SPIFFS, "/WebServer.html", (char *)pagina.c_str());
  }
  if (!SPIFFS.exists("/NoModif.html"))
  {
    writeFile(SPIFFS, "/NoModif.html", (char *)paginaNoModif.c_str());
  }

  // server
  if (!SPIFFS.exists("/IPServer.txt"))
  {
    writeFile(SPIFFS, "/IPServer.txt", (char *)serverAddress.c_str());
  }

  if (!SPIFFS.exists("/PortServer.txt"))
  {
    writeFile(SPIFFS, "/PortServer.txt", (char *)String(serverPort).c_str());
  }

  // router
  if (!SPIFFS.exists("/RouterSSID.txt"))
  {
    writeFile(SPIFFS, "/RouterSSID.txt", (char *)RouterSsid.c_str());
  }

  if (!SPIFFS.exists("/RouterPASS.txt"))
  {
    writeFile(SPIFFS, "/RouterPASS.txt", (char *)RouterPass.c_str());
  }

  // Config de cerradura
  if (!SPIFFS.exists("/ZONE.txt"))
  {
    writeFile(SPIFFS, "/ZONE.txt", "1");
  }
  if (!SPIFFS.exists("/DOOR.txt"))
  {
    writeFile(SPIFFS, "/DOOR.txt", "1");
  }
  if (!SPIFFS.exists("/CONTRASENA.txt"))
  {
    writeFile(SPIFFS, "/CONTRASENA.txt", "FFFFFFFFFFFFFFFFFFFFFFFF");
  }
  if (!SPIFFS.exists("/UIDsVetados.txt"))
  {
    writeFile(SPIFFS, "/UIDsVetados.txt", "");
  }

  answerNoModif = readFile(SPIFFS, "/NoModif.html");
  answer = readFile(SPIFFS, "/WebServer.html");
  SSID = readFile(SPIFFS, "/SSID.txt");
  PASSWORD = readFile(SPIFFS, "/PASS.txt");

  serverAddress = readFile(SPIFFS, "/IPServer.txt");
  serverPort = (readFile(SPIFFS, "/IPServer.txt")).toInt();
  RouterSsid = readFile(SPIFFS, "/RouterSSID.txt");
  RouterPass = readFile(SPIFFS, "/RouterPASS.txt");

  ZONA = readFile(SPIFFS, "/ZONE.txt");
  PUERTA = readFile(SPIFFS, "/DOOR.txt");
  procContrasena(readFile(SPIFFS, "/CONTRASENA.txt"));
  uidsVetados = readFile(SPIFFS, "/UIDsVetados.txt");

  // Se borraría en producto final
  Serial.print("Router: ");
  Serial.print(RouterSsid);
  Serial.print(" - ");
  Serial.println(RouterPass);

  Serial.print("Server: ");
  Serial.print(serverAddress);
  Serial.print(":");
  Serial.println(serverPort);

  Serial.print("Uids vetados:");
  Serial.println(uidsVetados);

  Serial.print("PASS: ");
  Serial.println(PASSWORD);
  Serial.print("ZONA ACTUAL: ");
  Serial.println(ZONA);
  Serial.print("PUERTA ACTUAL: ");
  Serial.println(PUERTA);
  Serial.println("CONTRAS: ");
  nfc.PrintHex(CONTRASENA, 12);
  nfc.PrintHex(keyA, 6);
  nfc.PrintHex(keyB, 6);
}

void procContrasena(String input)
{
  const char *hexString = input.c_str();
  for (int i = 0; i < 24; i += 2)
  {
    char hex[3] = {toupper(hexString[i]), toupper(hexString[i + 1]), '\0'};
    CONTRASENA[i / 2] = strtoul(hex, NULL, 16);
  }
  for (int i = 0; i < 12; i += 1)
  {
    if (i < 6)
      keyA[i] = CONTRASENA[i];
    else
      keyB[i - 6] = CONTRASENA[i];
  }
}

int updateUidsVetados()
{
  int res = 1;
  Serial.println("Conectando con servidor...");
  WiFiClient client;
  if (client.connect(serverAddress.c_str(), serverPort))
  {
    client.println("GET /uids HTTP/1.1");
    client.println("Host: " + String(serverAddress));
    client.println("Connection: close");
    client.println();

    // Leer y procesar la respuesta del servidor
    while (client.connected() && !client.available())
    {
      delay(100);
    }
    String newUids;
    while (client.available())
    { // Procesar la línea de la respuesta del servidor
      String line = client.readStringUntil('\n');
      newUids = line.substring(0, line.length() - 1);
    }
    if (newUids == "")
    {
      Serial.println("Respuesta del servidor vacío");
      res = 0;
    }
    else
    {
      uidsVetados = newUids;
      writeFile(SPIFFS, "/UIDsVetados.txt", (char *)uidsVetados.c_str());
      Serial.print("Uids vetados actualizados: ");
      Serial.println(uidsVetados);
    }
  }
  else
  {
    Serial.println("Error al conectar al servidor");
    res = 0;
  }
  client.stop();
  Serial.println("Conexión cerrada");
  vTaskResume(xLeerNFC);
  return res;
}

boolean estaUidVetado(uint8_t uid[], uint8_t UidLength)
{

  char valor[3];
  String uidS = "";
  for (int i = 0; i < UidLength; i++)
  {
    sprintf(valor, "%02X", uid[i]);
    uidS += valor;
  }
  if (strstr(uidsVetados.c_str(), uidS.c_str()) != NULL)
    return true;
  else
    return false;
}

// PROCESAR API's

void procSSID(AsyncWebServerRequest *request)
{
  String SSID1 = request->arg("ssid"); // server.arg("ssid");
  bool t = false;

  if (!SSID1.isEmpty())
  {
    writeFile(SPIFFS, "/SSID.txt", (char *)SSID1.c_str());
    Serial.println("SSID nuevo: " + SSID1);
    t = true;
  }
  else
    Serial.println("SSID no fue modificado");

  String PASS = request->arg("pass"); // server.arg("pass");
  if (PASS.isEmpty())
    Serial.println("La contraseña no fue modificada");
  else if (PASS.length() < 8)
    Serial.println("La contraseña es menor de 8 caracteres");
  else
  {
    writeFile(SPIFFS, "/PASS.txt", (char *)PASS.c_str());
    Serial.println("La contraseña fue cambiada correctamente");
    t = true;
  }

  if (t)
  {
    request->send(200, "text/plain", "RESTARTING IN 5 SECONDS");
    Serial.println("Restarting in 5 seconds");
    vTaskDelay(5000);

    ESP.restart();
  }
  else
    request->send(200, "text/html", answerNoModif);
}

void procLocation(AsyncWebServerRequest *request)
{
  // Obtenemos valores de la web
  String ZONA1 = request->arg("zone");

  if (!ZONA1.isEmpty() && ZONA != ZONA1)
  {
    writeFile(SPIFFS, "/ZONE.txt", (char *)ZONA1.c_str());
    ZONA = ZONA1;
    Serial.println("ZONA NUEVA " + ZONA1);
  }
  else
    Serial.println("La zona no fue modificada: " + ZONA);

  String PUERTA1 = request->arg("door");

  if (!PUERTA1.isEmpty() && PUERTA1 != PUERTA)
  {
    writeFile(SPIFFS, "/DOOR.txt", (char *)PUERTA1.c_str());
    PUERTA = PUERTA1;
    Serial.println("PUERTA NUEVA " + PUERTA1);
  }
  else
    Serial.println("La puerta no fue modificada: " + PUERTA);

  request->send(200, "text/html", answerNoModif);
}

void procPass(AsyncWebServerRequest *request)
{
  String cont = request->arg("pass");

  if (cont.length() != 24)
  {
    Serial.println("Contraseña vacía o mayor");
    return;
  }
  procContrasena(cont);
  writeFile(SPIFFS, "/CONTRASENA.txt", cont.c_str());

  request->send(200, "text/html", answerNoModif);
}

void procServer(AsyncWebServerRequest *request)
{
  String ipS = request->arg("ipServer");
  String portS = request->arg("portServer");

  writeFile(SPIFFS, "/IPServer.txt", (char *)ipS.c_str());
  writeFile(SPIFFS, "/PortServer.txt", (char *)portS.c_str());

  serverAddress = ipS;
  serverPort = portS.toInt();

  Serial.print("IP server: ");
  Serial.println(serverAddress);
  Serial.print("Port server: ");
  Serial.println(serverPort);

  request->send(200, "text/html", answerNoModif);
}

void procRouter(AsyncWebServerRequest *request)
{
  String ssidR = request->arg("ssidRouter");
  String passR = request->arg("passRouter");

  writeFile(SPIFFS, "/RouterSSID.txt", (char *)ssidR.c_str());
  writeFile(SPIFFS, "/RouterPASS.txt", (char *)passR.c_str());

  RouterSsid = ssidR;
  RouterPass = passR;

  Serial.println("SSID Router:" + ssidR);
  Serial.println("PASS Router:" + passR);

  request->send(200, "text/html", answerNoModif);
}