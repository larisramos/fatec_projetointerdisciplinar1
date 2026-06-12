#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <ESPmDNS.h>
#include <time.h> 

// =================================================================
// --- CONFIGURAÇÕES DO USUÁRIO ---
// =================================================================

// 1. DADOS WI-FI
const char* ssid = "arduino"; //Nome da Rede Wi-fi
const char* password = "arduino123"; //Senha da rede - 

// 2. DADOS DA API DE CLIMA (OpenWeatherMap)
String apiKey = "75efcb040f29bfb8858d9a05f19755a5"; 
String cidade = "Indaiatuba,BR";

// 3. PINOS DE CONEXÃO
#define SENSOR_SOLO_PIN 34
#define RELE_PIN 14
#define DHT_PIN 27 // <<< PINO ALTERADO PARA O GPIO 27 (MAIS SEGURO)

// 4. CONFIGURAÇÃO DHT11
#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);

// 5. CALIBRAÇÃO SENSOR DE SOLO
const int SENSOR_SECO = 4095;
const int SENSOR_MOLHADO = 1800;

// 6. SERVIDOR DE HORA (NTP) - Fuso Horário de São Paulo (UTC -3)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -10800; // -3 horas em segundos
const int   daylightOffset_sec = 0; // Sem horário de verão

// =================================================================
// --- VARIÁVEIS GLOBAIS DO SISTEMA ---
// =================================================================

WebServer server(80);

// Limites e Lógica de Controle
float umidadeMinima = 42.0; 
float umidadeMaxima = 65.0; 
String culturaAtual = "Salsinha";

// MÁQUINA DE ESTADOS DO SISTEMA
enum ModoOperacao { MODO_AUTO = 0, MODO_MANUAL_ON = 1, MODO_MANUAL_OFF = 2 };
ModoOperacao modoAtual = MODO_AUTO;

// Variáveis de Sensores
int umidadeSoloAtual = 0;
float tempAr = 0.0;
float umidAr = 0.0;
int releEstadoAtual = 0; 
bool estavaRegando = false; 

// Variáveis da API
String climaStatus = "Aguardando...";
bool previsaoChuva = false;

// Temporizadores (millis)
unsigned long previousMillisSensores = 0;
unsigned long previousMillisAPI = 0;
const long intervaloSensores = 5000;      
const long intervaloAPI = 900000;         

// Estrutura de Dados para o Histórico (Últimos 10)
struct RegaLog {
  String timestamp;
  String planta;
  float temp;
  String clima;
};
RegaLog historico[10];
int totalLogs = 0;
int indexLog = 0;

// =================================================================
// --- PÁGINA WEB (HTML, CSS, JS) - UI/UX ACESSÍVEL ---
// =================================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-br">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Controle de Estufa - Temperos</title>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Roboto:wght@400;700;900&display=swap');
    
    * { margin: 0; padding: 0; box-sizing: border-box; }
    
    body { 
      font-family: 'Roboto', sans-serif; background-color: #F0F4F8; 
      color: #102A43; display: flex; flex-direction: column; align-items: center; 
      padding: 20px; text-align: center; 
    }
    
    h1 { color: #0F5257; margin-bottom: 20px; font-size: 2.2rem; font-weight: 900; }
    
    .painel { display: flex; flex-wrap: wrap; justify-content: center; gap: 20px; max-width: 800px; width: 100%; }
    
    .card { 
      background: #FFFFFF; padding: 25px 15px; border-radius: 12px; 
      border: 2px solid #D9E2EC; box-shadow: 0 4px 6px rgba(0,0,0,0.05); flex: 1; min-width: 250px; 
    }
    
    h3 { font-size: 1.3rem; color: #334E68; margin-bottom: 10px;}
    .valor { font-size: 3.5rem; font-weight: 900; color: #0B6E4F; margin: 10px 0; line-height: 1; }
    .unidade { font-size: 1.2rem; color: #627D98; font-weight: 700; }
    
    .controles { 
      margin-top: 30px; background: #FFFFFF; padding: 25px; border-radius: 12px; 
      width: 100%; max-width: 800px; border: 2px solid #D9E2EC; box-shadow: 0 4px 6px rgba(0,0,0,0.05); 
    }
    
    select { 
      font-size: 1.2rem; font-weight: bold; padding: 15px; border-radius: 8px; 
      background: #F0F4F8; color: #102A43; border: 2px solid #9FB3C8; outline: none; 
      margin-bottom: 15px; cursor: pointer; width: 100%; max-width: 400px; appearance: auto; 
    }
    
    .modo-display {
      font-size: 1.1rem; background: #E2E8F0; color: #334E68; padding: 10px 20px;
      border-radius: 20px; margin-bottom: 15px; display: inline-block; font-weight: bold;
    }

    .botoes-acao { display: flex; flex-wrap: wrap; gap: 15px; margin-top: 15px; justify-content: center; }
    
    .btn { 
      font-size: 1.1rem; font-weight: bold; padding: 15px; border-radius: 8px; 
      border: none; cursor: pointer; width: 100%; max-width: 280px; 
      box-shadow: 0 4px 6px rgba(0,0,0,0.1); display: flex; justify-content: center; align-items: center; gap: 10px;
    }
    
    .btn-primario { background-color: #0B6E4F; color: white; }
    .btn-primario:active { background-color: #08553D; }
    
    .btn-perigo { background-color: #C62828; color: white; }
    .btn-perigo:active { background-color: #901B1B; }

    .btn-info { background-color: #2B6CB0; color: white; }
    .btn-info:active { background-color: #1A497A; }

    .btn-secundario { background-color: #627D98; color: white; }
    .btn-secundario:active { background-color: #486581; }

    .status-container { font-size: 1.3rem; font-weight: bold; margin-top: 25px; display: flex; flex-direction: column; align-items: center; gap: 10px; }
    .status-rele { padding: 10px 20px; border-radius: 8px; display: inline-block; text-transform: uppercase; letter-spacing: 1px; }
    .status-rele.ligado { background-color: #E3FCEF; color: #0B6E4F; border: 2px solid #0B6E4F; }
    .status-rele.desligado { background-color: #FFEEEE; color: #C62828; border: 2px solid #C62828; }
    
    .alerta { background-color: #FFF3E0; color: #E65100; padding: 15px; border-radius: 8px; border-left: 5px solid #FF9800; font-size: 1.1rem; margin-top: 20px; font-weight: bold; display: none; }
    .alerta.show { display: block; }

    /* TABELA DE HISTÓRICO */
    .tabela-container { overflow-x: auto; margin-top: 20px; border-radius: 8px; border: 1px solid #D9E2EC; }
    table { width: 100%; border-collapse: collapse; text-align: left; font-size: 1.1rem; }
    th { background-color: #D9E2EC; color: #102A43; padding: 15px; font-weight: bold; }
    td { padding: 15px; border-bottom: 1px solid #D9E2EC; color: #334E68; }
    tr:nth-child(even) { background-color: #F0F4F8; }
    
    footer { margin-top: 40px; font-size: 0.9rem; color: #829AB1; font-weight: bold;}
    .faixa-ideal { font-size: 1.1rem; color: #627D98; font-weight: bold; margin-top: 5px; }
    .cultura-destaque { font-size: 1.3rem; color: #102A43; }
  </style>
</head>
<body>
  <h1>Painel de Irrigação</h1>
  
  <div class="painel">
    <div class="card">
      <h3>Umidade do Solo</h3>
      <div class="valor" id="umidSoloValue">--<span class="unidade">%</span></div>
      <p class="cultura-destaque">Cultura: <strong id="culturaDisplay">Salsinha</strong></p>
      <div class="faixa-ideal" id="faixaDisplay">Ideal: 42% a 65%</div>
    </div>
    <div class="card">
      <h3>Clima Local (Estufa)</h3>
      <div class="valor" id="tempValue">--<span class="unidade">°C</span></div>
      <div style="font-size: 1.2rem; font-weight: bold; color: #627D98; margin-top: 10px;">
        Umidade Ar: <span id="umidArValue" style="color:#102A43;">--</span>%
      </div>
    </div>
    <div class="card">
      <h3>Previsão do Tempo</h3>
      <div class="valor" style="font-size: 2.2rem; color: #2B6CB0;" id="climaValue">--</div>
    </div>
  </div>

  <div class="controles">
    <h3>Configuração e Controle</h3>
    <p style="margin-bottom: 15px; font-size: 1rem; color: #627D98;">Toque abaixo para selecionar a planta:</p>
    
    <select id="plantaSelect" onchange="mudarPlanta()">
      <option value="50_75_Alface">Alface (Min: 50% | Máx: 75%)</option>
      <option value="30_50_Boldo">Boldo (Min: 30% | Máx: 50%)</option>
      <option value="32_55_Cebolinha">Cebolinha (Min: 32% | Máx: 55%)</option>
      <option value="48_70_Coentro">Coentro (Min: 48% | Máx: 70%)</option>
      <option value="45_70_Couve">Couve (Min: 45% | Máx: 70%)</option>
      <option value="40_65_Erva-Cidreira">Erva-Cidreira (Min: 40% | Máx: 65%)</option>
      <option value="55_80_Hortelã">Hortelã (Min: 55% | Máx: 80%)</option>
      <option value="40_65_Manjericão">Manjericão (Min: 40% | Máx: 65%)</option>
      <option value="45_70_Rúcula">Rúcula (Min: 45% | Máx: 70%)</option>
      <option value="42_65_Salsinha" selected>Salsinha (Min: 42% | Máx: 65%)</option>
      <option value="50_75_Tomate Cereja">Tomate Cereja (Min: 50% | Máx: 75%)</option>
    </select>

    <div class="modo-display" id="modoAtualText">Modo: Carregando...</div>

    <div class="botoes-acao">
      <button class="btn btn-primario" onclick="setModo('manual_on')">💧 Ligar Bomba</button>
      <button class="btn btn-perigo" onclick="setModo('manual_off')">🛑 Parar Bomba</button>
      <button class="btn btn-info" onclick="setModo('auto')">🤖 Modo Automático</button>
      <button class="btn btn-secundario" onclick="atualizarPagina()">🔄 Atualizar Página</button>
    </div>
    
    <div class="status-container">
      Status da Bomba: 
      <span id="statusRele" class="status-rele desligado">PARADA</span>
    </div>
    
    <div id="alertaChuva" class="alerta">
      Atenção: Irrigação automática suspensa devido à previsão de chuva para esta cultura!
    </div>
  </div>

  <div class="controles" style="margin-top: 20px;">
    <h3>Últimas 10 Irrigações</h3>
    <div class="tabela-container">
      <table id="tabelaHistorico">
        <thead>
          <tr>
            <th>Data e Hora</th>
            <th>Cultura</th>
            <th>Temp. Ambiente</th>
            <th>Clima</th>
          </tr>
        </thead>
        <tbody>
          <tr><td colspan="4" style="text-align:center;">Carregando histórico...</td></tr>
        </tbody>
      </table>
    </div>
  </div>

  <footer>FATEC INDAIATUBA | PI DESENVOLVIDO POR: Bruno Roelli, Larissa Ramos, Lucas Souza, Nicolas Vieira, Ryan Lima.</footer>

  <script>
    function atualizarPagina() {
      window.location.reload();
    }

    async function setModo(acao) {
      try {
        const res = await fetch(`/${acao}`);
        if (res.ok) {
          if (acao === 'manual_on') alert("Bomba LIGADA manualmente!\nEla desligará sozinha ao atingir o limite de segurança.");
          if (acao === 'manual_off') alert("Bomba PARADA e bloqueada.\nClique em 'Modo Automático' para o sensor voltar a funcionar.");
          if (acao === 'auto') alert("Sistema retornado para o MODO AUTOMÁTICO.");
          atualizarDados();
        } else {
          const msg = await res.text();
          alert("Aviso: " + msg);
        }
      } catch (error) {
        alert("Erro de conexão. Verifique o Wi-Fi.");
      }
    }

    async function mudarPlanta() {
      const select = document.getElementById('plantaSelect').value;
      const [umidMin, umidMax, nome] = select.split('_');
      
      try {
        await fetch(`/set?min=${umidMin}&max=${umidMax}&cultura=${nome}`);
        document.getElementById('culturaDisplay').innerText = nome;
        document.getElementById('faixaDisplay').innerText = `Ideal: ${umidMin}% a ${umidMax}%`;
      } catch (error) {
        alert('Erro ao atualizar a placa. Verifique a conexão Wi-Fi.');
      }
    }

    async function atualizarHistorico() {
      try {
        const res = await fetch('/historico');
        const logs = await res.json();
        const tbody = document.querySelector('#tabelaHistorico tbody');
        
        if (logs.length === 0) {
          tbody.innerHTML = '<tr><td colspan="4" style="text-align:center; padding: 20px;">Nenhuma irrigação registrada ainda.</td></tr>';
          return;
        }
        
        tbody.innerHTML = '';
        logs.forEach(log => {
          tbody.innerHTML += `<tr>
            <td><strong>${log.data}</strong></td>
            <td>${log.planta}</td>
            <td>${log.temp}°C</td>
            <td>${log.clima}</td>
          </tr>`;
        });
      } catch (e) { console.error('Erro historico:', e); }
    }

    async function atualizarDados() {
      try {
        const res = await fetch('/dados');
        const data = await res.json();
        
        document.getElementById('umidSoloValue').innerHTML = data.umidSolo + '<span class="unidade">%</span>';
        document.getElementById('tempValue').innerHTML = data.tempAr + '<span class="unidade">°C</span>';
        document.getElementById('umidArValue').innerText = data.umidAr;
        document.getElementById('climaValue').innerText = data.clima;
        document.getElementById('culturaDisplay').innerText = data.culturaAtual;
        document.getElementById('faixaDisplay').innerText = `Ideal: ${data.umidadeMinima}% a ${data.umidadeMaxima}%`;

        // Textos dos Modos (0 = Auto, 1 = Manual On, 2 = Manual Off)
        const modoTextos = ["Automático 🤖", "Ligado Manualmente 💧", "Parada Forçada (Bloqueada) 🛑"];
        document.getElementById('modoAtualText').innerText = "Modo: " + modoTextos[data.modo];

        const select = document.getElementById('plantaSelect');
        const valorEsperado = `${data.umidadeMinima}_${data.umidadeMaxima}_${data.culturaAtual}`;
        if(select.value !== valorEsperado && Array.from(select.options).some(opt => opt.value === valorEsperado)) {
            select.value = valorEsperado;
        }

        const statusReleEl = document.getElementById('statusRele');
        if (data.releEstado === 1) {
          statusReleEl.textContent = 'IRRIGANDO';
          statusReleEl.className = 'status-rele ligado';
        } else {
          statusReleEl.textContent = 'PARADA';
          statusReleEl.className = 'status-rele desligado';
        }

        const alertaEl = document.getElementById('alertaChuva');
        if (data.previsaoChuva === 1 && data.releEstado === 0 && data.umidSolo <= data.umidadeMinima && data.modo === 0) {
            alertaEl.classList.add('show');
        } else {
            alertaEl.classList.remove('show');
        }

      } catch (error) { console.error('Erro na atualização:', error); }
    }
    
    // Inicia os ciclos de atualização da tela
    setInterval(atualizarDados, 3000);
    setInterval(atualizarHistorico, 10000); 
    
    window.onload = () => {
      atualizarDados();
      atualizarHistorico();
    };
  </script>
</body>
</html>
)rawliteral";

// =================================================================
// --- FUNÇÕES GERAIS E DE SERVIDOR WEB ---
// =================================================================

String obterDataHoraAtual() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return "Aguardando relógio...";
  }
  char timeStringBuff[50];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%d/%m/%Y às %H:%M", &timeinfo);
  return String(timeStringBuff);
}

void registrarNoHistorico() {
  historico[indexLog].timestamp = obterDataHoraAtual();
  historico[indexLog].planta = culturaAtual;
  historico[indexLog].temp = tempAr;
  historico[indexLog].clima = climaStatus;

  indexLog = (indexLog + 1) % 10;
  if (totalLogs < 10) totalLogs++;
}

void handleRoot() { server.send_P(200, "text/html", index_html); }

void handleDados() {
  String json = "{";
  json += "\"umidSolo\":" + String(umidadeSoloAtual);
  json += ",\"tempAr\":" + String(tempAr, 1);
  json += ",\"umidAr\":" + String(umidAr, 1);
  json += ",\"clima\":\"" + climaStatus + "\"";
  json += ",\"releEstado\":" + String(releEstadoAtual);
  json += ",\"previsaoChuva\":" + String(previsaoChuva ? 1 : 0);
  json += ",\"culturaAtual\":\"" + culturaAtual + "\"";
  json += ",\"umidadeMinima\":" + String(umidadeMinima, 0);
  json += ",\"umidadeMaxima\":" + String(umidadeMaxima, 0);
  json += ",\"modo\":" + String((int)modoAtual);
  json += "}";
  server.send(200, "application/json", json);
}

void handleSet() {
  if (server.hasArg("min") && server.hasArg("max") && server.hasArg("cultura")) {
    umidadeMinima = server.arg("min").toFloat();
    umidadeMaxima = server.arg("max").toFloat();
    culturaAtual = server.arg("cultura");
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Argumentos ausentes");
  }
}

// ---- ROTAS DOS BOTÕES MANUAIS E AUTO ----
void handleManualOn() {
  if (umidadeSoloAtual >= umidadeMaxima) {
    server.send(400, "text/plain", "A terra já atingiu a umidade máxima segura.");
  } else {
    modoAtual = MODO_MANUAL_ON;
    server.send(200, "text/plain", "OK");
  }
}

void handleManualOff() {
  modoAtual = MODO_MANUAL_OFF;
  server.send(200, "text/plain", "OK");
}

void handleAuto() {
  modoAtual = MODO_AUTO;
  server.send(200, "text/plain", "OK");
}

// ------------------------------------------

void handleHistorico() {
  String json = "[";
  int countAExibir = totalLogs;
  int i = (indexLog - 1 + 10) % 10; 
  
  for (int j = 0; j < countAExibir; j++) {
    json += "{";
    json += "\"data\":\"" + historico[i].timestamp + "\",";
    json += "\"planta\":\"" + historico[i].planta + "\",";
    json += "\"temp\":" + String(historico[i].temp, 1) + ",";
    json += "\"clima\":\"" + historico[i].clima + "\"";
    json += "}";
    if (j < countAExibir - 1) json += ",";
    
    i = (i - 1 + 10) % 10;
  }
  json += "]";
  server.send(200, "application/json", json);
}

// =================================================================
// --- INTEGRAÇÃO COM API DE CLIMA ---
// =================================================================

void consultarPrevisaoDoTempo() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String serverPath = "http://api.openweathermap.org/data/2.5/weather?q=" + cidade + "&APPID=" + apiKey + "&units=metric&lang=pt_br";
    
    http.begin(serverPath);
    int httpResponseCode = http.GET();
    
    if (httpResponseCode == 200) {
      String payload = http.getString();
      JsonDocument doc;
      deserializeJson(doc, payload);
      
      climaStatus = doc["weather"][0]["description"].as<String>();
      
      int weatherId = doc["weather"][0]["id"].as<int>();
      previsaoChuva = (weatherId < 600);
    }
    http.end();
  }
}

// =================================================================
// --- LÓGICA PRINCIPAL DE IRRIGAÇÃO E SEGURANÇA ---
// =================================================================

void lerSensoresEIrrigar() {
  // 1. Lê o Sensor de Solo
  int valorAnalogico = analogRead(SENSOR_SOLO_PIN);
  umidadeSoloAtual = map(valorAnalogico, SENSOR_SECO, SENSOR_MOLHADO, 0, 100);
  umidadeSoloAtual = constrain(umidadeSoloAtual, 0, 100);

  // 2. Lê o DHT11
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t) && !isnan(h)) {
    tempAr = t;
    umidAr = h;
  }

  // 3. Trava de Segurança Global
  if (modoAtual == MODO_MANUAL_ON && umidadeSoloAtual >= umidadeMaxima) {
      modoAtual = MODO_AUTO;
      Serial.println("SEGURANÇA: Rega manual desativada. Limite máximo atingido.");
  }

  // 4. Lógica da Máquina de Estados
  bool ligarBomba = (releEstadoAtual == 1); 

  if (modoAtual == MODO_MANUAL_ON) {
      ligarBomba = true;
  } 
  else if (modoAtual == MODO_MANUAL_OFF) {
      ligarBomba = false;
  } 
  else { 
      // MODO AUTOMÁTICO (Sensor Assume)
      if (umidadeSoloAtual <= umidadeMinima) {
          ligarBomba = true;  
      } else if (umidadeSoloAtual >= umidadeMaxima) {
          ligarBomba = false; 
      }

      // Bloqueio Preventivo por Chuva (Apenas no Automático)
      if (ligarBomba && previsaoChuva) {
          if (culturaAtual == "Salsinha" || culturaAtual == "Cebolinha" || 
              culturaAtual == "Boldo" || culturaAtual == "Tomate Cereja") {
              ligarBomba = false;
          }
      }
  }

  // 5. Acionamento Físico do Relé
  if (ligarBomba) {
      digitalWrite(RELE_PIN, LOW);
  } else {
      digitalWrite(RELE_PIN, HIGH);
  }
  
  releEstadoAtual = ligarBomba ? 1 : 0;
  
  // 6. Registro no Histórico 
  if (releEstadoAtual == 1 && !estavaRegando) {
      registrarNoHistorico();
  }
  estavaRegando = (releEstadoAtual == 1);
}

// =================================================================
// --- SETUP E LOOP ---
// =================================================================

void setup() {
  Serial.begin(115200);
  
  pinMode(RELE_PIN, OUTPUT);
  digitalWrite(RELE_PIN, HIGH); 
  
  dht.begin(); 

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("\nConectando Wi-Fi");

  byte tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConectado! IP: " + WiFi.localIP().toString());
    
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    if (!MDNS.begin("irrigacao")) {
      Serial.println("Erro ao configurar o mDNS.");
    } else {
      MDNS.addService("http", "tcp", 80); 
    }

    server.on("/", handleRoot);
    server.on("/dados", handleDados);
    server.on("/set", HTTP_GET, handleSet);
    
    // Rotas de Controle
    server.on("/manual_on", HTTP_GET, handleManualOn);
    server.on("/manual_off", HTTP_GET, handleManualOff);
    server.on("/auto", HTTP_GET, handleAuto);
    
    server.on("/historico", HTTP_GET, handleHistorico); 

    server.begin();
    consultarPrevisaoDoTempo(); 
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }

  unsigned long currentMillis = millis();
  
  if (currentMillis - previousMillisSensores >= intervaloSensores) {
    previousMillisSensores = currentMillis; 
    lerSensoresEIrrigar();
  }

  if (currentMillis - previousMillisAPI >= intervaloAPI) {
    previousMillisAPI = currentMillis;
    consultarPrevisaoDoTempo();
  }
}