#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <Bounce2.h>
#include "internet.h"
#include "certificados.h"

// Define o número total de botões conectados // 

#define num_botoes 7

// Define os pinos do ESP32 utilizados para cada botão // 

#define pinBotaoA 4
#define pinBotaoB 5
#define pinBotaoC 6
#define pinBotaoD 7
#define pinBotaoE 15
#define pinBotaoF 16
#define pinBotaoK 17

// Define os pinos analógicos utilizados para os eixos X e Y do joystick //
#define pinAnalogicoX 18
#define pinAnalogicoy 8

// Vetor com os pinos dos botões (facilita a iteração no código) //

const char pinJoystick[num_botoes] = {
    pinBotaoA,
    pinBotaoB,
    pinBotaoC,
    pinBotaoD,
    pinBotaoE,
    pinBotaoF,
    pinBotaoK};

const int mqtt_port = 8883;
const char *mqtt_id = "Amanda_esp32";
const char *mqtt_SUB = "carrinho/controle";
const char *mqtt_PUB = "carrinho/controle";

WiFiClientSecure espClient;
PubSubClient mqtt(espClient);
Bounce *joystick = new Bounce[num_botoes]; // Vetor de objetos Bounce para fazer debounce dos botões
JsonDocument doc;

enum pinsBotoes
{
  botaoA = 0,
  botaoB = 1,
  botaoC = 2,
  botaoD = 3,
  botaoE = 4,
  botaoF = 5,
  botaok = 6
};

void conectaMQTT();
void Callback(char *, byte *, unsigned int);

void setup()
{
  Serial.begin(9600);
  conectaWiFi();

  espClient.setCACert(AWS_ROOT_CA);
  espClient.setCertificate(AWS_CERT);
  espClient.setPrivateKey(AWS_KEY);
  mqtt.setServer(AWS_BROKER, mqtt_port);
  mqtt.setCallback(Callback);

  for (char i = 0; i < num_botoes; i++)
    joystick[i].attach(pinJoystick[i], INPUT); // Configura pino como entrada
}

void loop()
{
  checkWiFi();

  if (!mqtt.connected())
    conectaMQTT();

  mqtt.loop();

  bool atualizacao = 0;                                     // Flag que indica se houve alguma alteração no estado dos botões ou joystick
  bool alteracaoBotoes[num_botoes] = {0, 0, 0, 0, 0, 0, 0}; // Armazena quais botões mudaram
  bool estadoBotoes[num_botoes] = {0, 0, 0, 0, 0, 0, 0};    // Armazena o estado atual (pressionado ou não) de cada botão

  // Variável estática para guardar os valores anteriores dos eixos analógicos (X e Y) // 

  static int analogicoAntes[2] = {9, 9};

  // Leitura atual dos eixos X e Y, com divisão para reduzir resolução e ruído // 

  int analogicoAtual[2] = {analogRead(pinAnalogicoX) / 200, analogRead(pinAnalogicoy) / 200};

  doc.clear(); // Limpa o objeto JSON para a nova leitura

  // Verifica o estado de cada botão // 

  for (char j = 0; j < num_botoes; j++)
  {
    joystick[j].update(); // Atualiza o estado do botão com debounce

    if (joystick[j].changed()) // Se houve alteração no estado do botão
    {
      char chave[10];                       // Chave para o campo JSON correspondente ao botão
      sprintf(chave, "botao%d", j);         // Cria uma chave do tipo "botao0", "botao1", e vai até o "botao6".
      doc[chave] = !joystick[j].read();     // Armazena o novo estado no JSON
      alteracaoBotoes[j] = 1;               // Marca que esse botão teve alteração
      estadoBotoes[j] = joystick[j].read(); // Atualiza o estado armazenado
      atualizacao = 1;                      // Marca que houve alguma atualização98
    }
  }

    //  ------------------------------ Leitura analógica com EMA (sem delay) ------------------------------------- //

  // Leitura bruta reduzida (igual ao que você usava: /200)

  int rawX = analogRead(pinAnalogicoX) / 200;
  int rawY = analogRead(pinAnalogicoy) / 200;

  // EMA (filtro exponencial) — não bloqueante

  static float emaX = 0.0f, emaY = 0.0f;
  static bool firstRun = true;
  const float alpha = 0.25f; // quanto maior alfa, mais responsivo; menor alfa = mais suavização

  if (firstRun) {
    emaX = rawX;
    emaY = rawY;
    firstRun = false;
  } else {
    emaX = emaX * (1.0f - alpha) + rawX * alpha;
    emaY = emaY * (1.0f - alpha) + rawY * alpha;
  }

  int smoothX = (int)(emaX + 0.5f); // valor suavizado inteiro
  int smoothY = (int)(emaY + 0.5f);

  // -------------------- Dead-zone sensível e heartbeat -------------------- //

  int diferencaX = abs(smoothX - analogicoAntes[0]);
  int diferencaY = abs(smoothY - analogicoAntes[1]);

  // threshold menor: >0 detecta qualquer mudança no valor suavizado //

  const int DEADZONE = 0;

  if (diferencaX > DEADZONE) {
    doc["AnalogX"] = smoothX;
    atualizacao = 1;
  }
  if (diferencaY > DEADZONE) {
    doc["AnalogY"] = smoothY;
    atualizacao = 1;
  }

  // Heartbeat: garante que a cada intervalo publicamos algo mesmo sem mudança //

  static unsigned long lastHeartbeat = 0;
  const unsigned long HEARTBEAT_INTERVAL = 500; // ms (ajuste se quiser mais/menos)
  if (!atualizacao && (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL)) {
    doc["AnalogX"] = smoothX;
    doc["AnalogY"] = smoothY;
    atualizacao = 1;
    lastHeartbeat = millis();
  }

  // Atualiza os valores anteriores com os novos (apenas quando mudaram/foram publicados) // 

  if (atualizacao) {
    analogicoAntes[0] = smoothX;
    analogicoAntes[1] = smoothY;

    String msg;
    serializeJson(doc, msg);
    mqtt.publish(mqtt_PUB, msg.c_str());
    doc.clear(); // opcional: limpa doc após publicar
  }
}

void Callback(char *topic, byte *payload, unsigned int Length)
{
  String msg((char *)payload, Length);
  Serial.printf("Mensagem recebida (topico: [%s]): %s\n\r", topic, msg.c_str());
}

void conectaMQTT()
{
  while (!mqtt.connected())
  {
    Serial.print("Conectando ao AWS Iot Core ...");

    if (mqtt.connect(mqtt_id))
    {
      Serial.print("conectado.");
      mqtt.subscribe(mqtt_SUB);
    }
    else
    {
      Serial.printf("falhou (%d). Tentando novamente em 5s \n\r", mqtt.state());
      delay(5000);
    }
  }
}