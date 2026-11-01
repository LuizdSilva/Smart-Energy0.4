

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// CONFIGURACAO MQTT

const char* servidorMQTT = "broker.emqx.io";
const int portaMQTT = 1883;

const char* topicoEstado =
  "smartenergy/facens/demo-48271/sala-a01/estado";

const char* topicoComando =
  "smartenergy/facens/demo-48271/sala-a01/comando";

WiFiClient clienteWiFi;
PubSubClient mqtt(clienteWiFi);

unsigned long ultimaTentativaMQTT = 0;
unsigned long ultimaPublicacao = 0;

const int sensorPIR = 13;

const int ledLuz = 26;
const int ledAr = 27;

const int botaoLuz = 32;
const int botaoAr = 33;

// 20 segundos para demonstracao
// 600000 = 10 minutos reais
const unsigned long tempoLimite = 20000;

const unsigned long debounce = 200;

unsigned long inicioAusencia = 0;
unsigned long ultimoCliqueLuz = 0;
unsigned long ultimoCliqueAr = 0;

bool luzLigada = true;
bool arLigado = true;

bool movimentoAnterior = false;
bool movimentoJaDetectado = false;
bool contagemIniciada = false;
bool alertaGerado = false;

bool estadoAnteriorBotaoLuz = HIGH;
bool estadoAnteriorBotaoAr = HIGH;

void atualizarEquipamentos() {
  digitalWrite(ledLuz, luzLigada ? HIGH : LOW);
  digitalWrite(ledAr, arLigado ? HIGH : LOW);
}

void mostrarEstados() {
  Serial.print("Iluminacao: ");
  Serial.println(luzLigada ? "LIGADA" : "DESLIGADA");

  Serial.print("Ar-condicionado: ");
  Serial.println(arLigado ? "LIGADO" : "DESLIGADO");
}

void conectarWiFi() {
  Serial.println("Conectando ao Wi-Fi...");

  WiFi.begin("Wokwi-GUEST", "", 6);

  unsigned long inicio = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - inicio < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("Wi-Fi conectado!");
    Serial.print("Endereco IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("Falha ao conectar ao Wi-Fi.");
  }
}

void conectarMQTT() {

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (mqtt.connected()) {
    return;
  }

  // Evita tentativas constantes de reconexao
  if (ultimaTentativaMQTT != 0 &&
      millis() - ultimaTentativaMQTT < 5000) {
    return;
  }

  ultimaTentativaMQTT = millis();

  Serial.print("Conectando ao broker MQTT... ");

  if (mqtt.connect("smartenergy-a01-wokwi-48271")) {
  Serial.println("MQTT conectado!");

  if (mqtt.subscribe(topicoComando)) {
    Serial.println("Inscrito no topic de comandos!");
  } else {
    Serial.println("Falha ao inscrever no topic.");
  }
} else {
    Serial.print("Falha MQTT. Codigo: ");
    Serial.println(mqtt.state());
  }
}

void publicarEstado() {

  if (!mqtt.connected()) {
    return;
  }

  // Envia atualizacoes a cada 3 segundos
  if (millis() - ultimaPublicacao < 3000) {
    return;
  }

  ultimaPublicacao = millis();

  String mensagem = "{";
  mensagem += "\"predio\":\"A\",";
  mensagem += "\"sala\":\"A01\",";

  mensagem += "\"movimento\":";
  mensagem += (digitalRead(sensorPIR) == HIGH
               ? "true" : "false");

  mensagem += ",\"iluminacao\":";
  mensagem += (luzLigada ? "true" : "false");

  mensagem += ",\"ar_condicionado\":";
  mensagem += (arLigado ? "true" : "false");

  mensagem += ",\"alerta\":";
  mensagem += (alertaGerado ? "true" : "false");

  mensagem += "}";

  bool enviado = mqtt.publish(
    topicoEstado,
    mensagem.c_str()
  );

  if (enviado) {
    Serial.println("MQTT: " + mensagem);
  } else {
    Serial.println("Falha ao publicar MQTT.");
  }
}

void receberComando(char* topico, byte* payload,
                    unsigned int tamanho) {

  if (String(topico) != topicoComando) {
    return;
  }

  if (tamanho == 0 || tamanho > 200) {
    Serial.println("Tamanho de comando invalido.");
    return;
  }

  JsonDocument doc;

  DeserializationError erro =
      deserializeJson(doc, payload, tamanho);

  if (erro) {
    Serial.println("Erro ao interpretar JSON!");
    return;
  }

  if (!doc.is<JsonObject>()) {
    Serial.println("Comando precisa ser um objeto JSON.");
    return;
  }

  String equipamento = doc["equipamento"] | "";
  String acao = doc["acao"] | "";

  Serial.println("Comando JSON recebido:");
  Serial.println("Equipamento: " + equipamento);
  Serial.println("Acao: " + acao);

  // CONTROLE DA ILUMINACAO
  if (equipamento == "iluminacao") {

    if (acao == "ligar") {
      luzLigada = true;

    } else if (acao == "desligar") {
      luzLigada = false;

    } else {
      Serial.println("Acao invalida!");
      return;
    }

  // CONTROLE DO AR-CONDICIONADO
  } else if (equipamento == "ar_condicionado") {

    if (acao == "ligar") {
      arLigado = true;

    } else if (acao == "desligar") {
      arLigado = false;

    } else {
      Serial.println("Acao invalida!");
      return;
    }

  // CONTROLE GERAL
  } else if (equipamento == "todos" &&
             acao == "desligar") {

    luzLigada = false;
    arLigado = false;

  } else {
    Serial.println("Comando desconhecido!");
    return;
  }

  atualizarEquipamentos();
  mostrarEstados();

  Serial.println("Comando JSON executado!");
}

void setup() {
  Serial.begin(115200);

  conectarWiFi();
  mqtt.setServer(servidorMQTT, portaMQTT);
  
  mqtt.setCallback(receberComando);

  pinMode(sensorPIR, INPUT);

  pinMode(ledLuz, OUTPUT);
  pinMode(ledAr, OUTPUT);

  pinMode(botaoLuz, INPUT_PULLUP);
  pinMode(botaoAr, INPUT_PULLUP);

  atualizarEquipamentos();

  Serial.println("SMART ENERGY - FACENS");
  Serial.println("Sala 102 iniciada.");

  mostrarEstados();

  Serial.println("Aguardando movimento...");
}

void loop() {

conectarMQTT();

if (mqtt.connected()) {
  mqtt.loop();
}

  unsigned long agora = millis();

  // ---------------------------
  // CONTROLE DA ILUMINACAO
  // ---------------------------

  bool leituraLuz = digitalRead(botaoLuz);

  if (estadoAnteriorBotaoLuz == HIGH &&
      leituraLuz == LOW &&
      agora - ultimoCliqueLuz > debounce) {

    luzLigada = !luzLigada;
    ultimoCliqueLuz = agora;

    atualizarEquipamentos();

    Serial.println("Botao da iluminacao pressionado.");
    mostrarEstados();
  }

  estadoAnteriorBotaoLuz = leituraLuz;

  // ---------------------------
  // CONTROLE DO AR-CONDICIONADO
  // ---------------------------

  bool leituraAr = digitalRead(botaoAr);

  if (estadoAnteriorBotaoAr == HIGH &&
      leituraAr == LOW &&
      agora - ultimoCliqueAr > debounce) {

    arLigado = !arLigado;
    ultimoCliqueAr = agora;

    atualizarEquipamentos();

    Serial.println("Botao do ar-condicionado pressionado.");
    mostrarEstados();
  }

  estadoAnteriorBotaoAr = leituraAr;

  // ---------------------------
  // SENSOR DE MOVIMENTO
  // ---------------------------

  bool movimento = digitalRead(sensorPIR) == HIGH;

  if (movimento) {

    movimentoJaDetectado = true;

    if (!movimentoAnterior) {
      Serial.println("Movimento detectado!");

      alertaGerado = false;
    }

    // Cancelar a contagem
    contagemIniciada = false;

  } else {

    if (movimentoJaDetectado &&
        !contagemIniciada &&
        !alertaGerado &&
        (luzLigada || arLigado)) {

      inicioAusencia = agora;
      contagemIniciada = true;

      Serial.println("Movimento ausente.");
      Serial.println("Iniciando contagem...");
    }
  }

  movimentoAnterior = movimento;

  // ---------------------------
  // VERIFICACAO DE DESPERDICIO
  // ---------------------------

  if (contagemIniciada &&
      !movimento &&
      !alertaGerado) {

    // Nao ha desperdicio se tudo esta desligado
    if (!luzLigada && !arLigado) {

      contagemIniciada = false;

      Serial.println("Todos os equipamentos desligados.");
      Serial.println("Contagem cancelada.");

    } else if (agora - inicioAusencia >= tempoLimite) {

      Serial.println("--------------------------");
      Serial.println("ALERTA SMART ENERGY!");
      Serial.println("Possivel desperdicio na sala 102.");

      if (luzLigada) {
        Serial.println("Desligando iluminacao...");
        luzLigada = false;
      }

      if (arLigado) {
        Serial.println("Desligando ar-condicionado...");
        arLigado = false;
      }

      atualizarEquipamentos();

      mostrarEstados();

      Serial.println("Ocorrencia registrada.");
      Serial.println("--------------------------");

      alertaGerado = true;
      contagemIniciada = false;
    }
  }

  // Inicia nova contagem se algum equipamento
  // for ligado durante a ausencia de movimento

  if (!movimento &&
      movimentoJaDetectado &&
      !contagemIniciada &&
      alertaGerado &&
      (luzLigada || arLigado)) {

    alertaGerado = false;
    contagemIniciada = true;
    inicioAusencia = agora;

    Serial.println("Equipamento religado.");
    Serial.println("Nova contagem iniciada.");
  }

  publicarEstado();

  delay(20);
}
