#include <Arduino.h>
#include <WiFi.h>
#include "internet.h"
#include "senhas.h"


//*------CONFIGURACAO DO WI-FI------

const unsigned long tempoEsperaConexao = 20000;   // tempo máximo (em ms) para tentar conectar ao Wi-Fi
const unsigned long tempoEsperaReconexao = 10000; // intervalo de tempo (em ms) entre tentativas de reconexão

void conectaWiFi()
{
  Serial.printf("Conectando ao WiFi: %s", SSID); // conectando ao Wifi "MQTT"

  WiFi.begin(SSID, SENHA); // conectado o Wifi

  unsigned long tempoInicialWiFi = millis(); // armazena o tempo inicial da tentativa de conexão

  while (WiFi.status() != WL_CONNECTED && millis() - tempoInicialWiFi < tempoEsperaConexao) // retornar numero 3, para "conectado"
  {
    Serial.print(".");
    delay(500); // espera 500 ms antes de tentar novamente
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWiFi Conectado com sucesso! "); // conectado
    Serial.print("Ëndereco IP:");                     // Mostra o endereco do IP
    Serial.println(WiFi.localIP());
  }
  else
    Serial.println("\nFalha ao conectar no WiFi. Verifique o nome da rede e a senha");
}

void checkWiFi()
{
  unsigned long tempoAtual = millis();         // obtém o tempo atual em ms desde que o ESP32 foi ligado
  static unsigned long tempoUltimaConexao = 0; // armazena o tempo da última tentativa de reconexão

  if (tempoAtual - tempoUltimaConexao > tempoEsperaReconexao) // verifica se já passou o tempo de espera para nova tentativa
  {
    if (WiFi.status() != WL_CONNECTED) // se não estiver conectado
    {
      Serial.println("\n Conexao Perdida! Tentando Reconectar...");
      conectaWiFi(); // tenta reconectar ao Wi-Fi
    }
    tempoUltimaConexao = tempoAtual; // atualiza o tempo da última tentativa
  }
}