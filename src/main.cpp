#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <Bounce2.h>
#include <LiquidCrystal_I2C.h>
#include <ezTime.h>
#include "internet.h"
#include "certificados.h"

// Define o número total de botões conectados //

#define num_botoes 7

// Define os pinos do ESP32 utilizados para cada botão //

#define pinBotaoA 32
#define pinBotaoB 33
#define pinBotaoC 25
#define pinBotaoD 26
#define pinBotaoE 27
#define pinBotaoF 14
#define pinBotaoK 4

// Define os pinos analógicos utilizados para os eixos X e Y do joystick //
#define pinAnalogicoX 34
#define pinAnalogicoy 35

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
const char *mqtt_id = "Amandinhaa_esp32";
const char *mqtt_SUB_controle = "carrinho/controle";
const char *mqtt_PUB_controle = "carrinho/controle";
const char *mqtt_SUB_dash = "carrinho/dash";
const char *mqtt_PUB_dash = "carrinho/dash";

WiFiClientSecure espClient;
PubSubClient mqtt(espClient);
Bounce *joystick = new Bounce[num_botoes]; // Vetor de objetos Bounce para fazer debounce dos botões

LiquidCrystal_I2C lcd(0x27, 20, 4);
Timezone tempo;

bool atualizacaoSenha = 0;
bool senhaAtivado = false;
bool senhaAtivadoAntes = 0;
int senha = 0;
String nomeUsuario = "";

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
void programaSenha();

void setup()
{
  Serial.begin(9600);
  conectaWiFi();

  tempo.setLocation("America/Sao_Paulo");
  waitForSync();

  lcd.init();
  lcd.backlight();
  // lcd.setCursor(0, 0);
  // lcd.print("Nome:");
  lcd.setCursor(0, 1);
  lcd.print("Codigo:");

  espClient.setCACert(AWS_ROOT_CA);
  espClient.setCertificate(AWS_CERT);
  espClient.setPrivateKey(AWS_KEY);
  mqtt.setBufferSize(2048);
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
  JsonDocument doc;

  programaSenha();

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

  // Leitura bruta reduzida (igual ao que você usava: /200)

  int leituraBrutaX = analogRead(pinAnalogicoX) / 200;
  int leituraBrutaY = analogRead(pinAnalogicoy) / 200;

  // filtro (filtro exponencial) — não bloqueante

  static float filtroX = 0.0f, filtroY = 0.0f;
  static bool firstRun = true;
  const float alfa = 0.25f; // quanto maior alfa, mais responsivo; menor alfa = mais suavização

  if (firstRun)
  {
    filtroX = leituraBrutaX;
    filtroY = leituraBrutaY;
    firstRun = false;
  }
  else
  {
    filtroX = filtroX * (1.0f - alfa) + leituraBrutaX * alfa;
    filtroY = filtroY * (1.0f - alfa) + leituraBrutaY * alfa;
  }

  int suavizacaoX = (int)(filtroX + 0.5f); // valor suavizado inteiro
  int suavizacaoY = (int)(filtroY + 0.5f);

  // -------------------- Dead-zone sensível e heartbeat -------------------- //

  int diferencaX = abs(suavizacaoX - analogicoAntes[0]);
  int diferencaY = abs(suavizacaoY - analogicoAntes[1]);

  // threshold menor: >0 detecta qualquer mudança no valor suavizado //

  const int ponto_morto = 0;

  if (diferencaX > ponto_morto)
  {
    doc["AnalogX"] = suavizacaoX;
    atualizacao = 1;
  }
  if (diferencaY > ponto_morto)
  {
    doc["AnalogY"] = suavizacaoY;
    atualizacao = 1;
  }

  // Heartbeat: garante que a cada intervalo publicamos algo mesmo sem mudança //

  static unsigned long ultimaMudanca = 0;
  const unsigned long intervalo = 500; // ms (ajuste se quiser mais/menos)
  if (!atualizacao && (millis() - ultimaMudanca >= intervalo))
  {
    doc["AnalogX"] = suavizacaoX;
    doc["AnalogY"] = suavizacaoY;

    atualizacao = 1;
    ultimaMudanca = millis();
  }

  // Atualiza os valores anteriores com os novos (apenas quando mudaram/foram publicados) //

  if (atualizacao)
  {
    analogicoAntes[0] = suavizacaoX;
    analogicoAntes[1] = suavizacaoY;

    String msg;
    serializeJson(doc, msg);
    mqtt.publish(mqtt_PUB_controle, msg.c_str());
  }
}

void Callback(char *topic, byte *payload, unsigned int Length)
{
  String msg((char *)payload, Length);
  Serial.printf("Mensagem recebida (topico: [%s]): %s\n\r", topic, msg.c_str());

  Serial.println(msg);
  msg.trim();

  JsonDocument doc;

  DeserializationError erro = deserializeJson(doc, msg);

  if (erro)
    Serial.printf("Erro %s no formato json", erro.c_str());

  else
  {
    if (!doc["senha"].isNull())
    {
      if (!senhaAtivado)
      {
        String senhaString = doc["senha"];
        senha = senhaString.toInt();

        atualizacaoSenha = 1;
      }
    }

    if (!doc["nome"].isNull())
    {
      if (nomeUsuario == "")
      {
        String nome = doc["nome"];
        nomeUsuario = nome;
      }
    }
  }
}

void conectaMQTT()
{
  while (!mqtt.connected())
  {
    Serial.print("Conectando ao AWS Iot Core ...");

    if (mqtt.connect(mqtt_id))
    {
      Serial.print("conectado.");
      mqtt.subscribe(mqtt_SUB_controle);
      mqtt.subscribe(mqtt_SUB_dash);
    }
    else
    {
      Serial.printf("falhou (%d). Tentando novamente em 5s \n\r", mqtt.state());
      delay(5000);
    }
  }
}

void programaSenha()
{
  static int senhaAtualizar = random(1000, 9999);
  static int segundos = 0;
  static int minutos = 0;
  static int intervalo = 0;

  static unsigned long tempoAntes = 0;
  static unsigned long tempoAntes02 = 0;
  unsigned long agora = millis();

  if (senha == senhaAtualizar)
  {

    if (millis() - tempoAntes > 2000)
    {
      if (nomeUsuario == nomeUsuario)
      {
        if (!senhaAtivado)
        {
          minutos += 2; // adiciona 5 minutos só uma vez
          senhaAtivado = true;
          atualizacaoSenha = 1;
        }
      }
    }
  }

  if (agora - tempoAntes02 > 1000)
  {
    atualizacaoSenha = 1;
    --segundos;

    if (segundos < 0)
    {
      --minutos;
      segundos = 59;
    }

    if (minutos < 0)
    {
      minutos = 0;
      segundos = 30;

      senhaAtualizar = random(1000, 9999);
      senhaAtivado = false;
      nomeUsuario = "";
    }

    tempoAntes02 = agora;
  }

  if (atualizacaoSenha)
  {
    lcd.setCursor(15, 0);
    lcd.printf("%02d:%02d", minutos, segundos);

    if (!senhaAtivado)
    {
      lcd.setCursor(0, 1);
      lcd.printf("Codigo: %d             ", senhaAtualizar);

      lcd.setCursor(0, 3);
      lcd.print("          ");
    }

    else
    {
      if (nomeUsuario == nomeUsuario)
      {
        lcd.setCursor(0, 1);
        lcd.printf("Nome: %s       ", nomeUsuario);
      }

      lcd.setCursor(0, 3);
      lcd.print("Sucesso!!!");
    }

    JsonDocument doc;

    doc["estado_acesso"] = senhaAtivado;

    String msg;

    if (senhaAtivado != senhaAtivadoAntes)
    {
      serializeJson(doc, msg);
      mqtt.publish(mqtt_PUB_dash, msg.c_str());

      senhaAtivadoAntes = senhaAtivado;
    }

    atualizacaoSenha = 0;
  }
}
