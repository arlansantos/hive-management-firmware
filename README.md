# Hive Management Firmware 🐝

Firmware do nó sensor embarcado desenvolvido para o sistema **Colmeia Digital**, um sistema de gestão e monitoramento remoto de colmeias de abelhas (*Apis mellifera*), proposto como Trabalho de Conclusão de Curso em Engenharia de Computação.

Este repositório contém **exclusivamente o código do firmware**, responsável pela aquisição de dados em campo, gerenciamento energético, resiliência à falha de comunicação e envio das informações à plataforma em nuvem via protocolo MQTT.

---

## 📌 Visão Geral

O firmware é executado em um microcontrolador **ESP32-S3** e foi projetado para operar de forma autônoma em ambientes remotos, com foco em:

- Baixo consumo energético (*Deep Sleep*)
- Coleta periódica de dados ambientais e de peso
- Comunicação sem fio via Wi-Fi e MQTT
- Resiliência a falhas de conectividade (buffer local)
- Configuração simplificada em campo via *Captive Portal*

---

## 🧠 Funcionalidades Principais

- Leitura de sensores:
  - Peso da colmeia (HX711 + células de carga)
  - Temperatura e umidade internas (AHT10)
  - Temperatura externa (DS18B20)
- Geração de *timestamp* preciso (RTC DS3231 + NTP)
- Formatação dos dados em **JSON padronizado**
- Envio dos dados para broker MQTT
- Armazenamento local em caso de falha de conexão
- Reenvio automático dos dados armazenados
- Gerenciamento energético com desligamento físico dos sensores
- Interface de configuração local via Wi-Fi (WiFiManager)
- Interação por botão físico com feedback sonoro (buzzer)

---

## 🧩 Arquitetura de Execução

O firmware **não utiliza o loop infinito tradicional do Arduino**.  
Toda a lógica de execução ocorre dentro da função `setup()`, seguindo o fluxo:

1. Identificação da causa do despertar (timer ou botão)
2. Energização dos sensores
3. Leitura dos sensores
4. Sincronização de horário (RTC / NTP)
5. Tentativa de conexão Wi-Fi e MQTT
6. Envio dos dados ou armazenamento em buffer
7. Desligamento dos periféricos
8. Entrada em modo **Deep Sleep**

---

## 🔋 Gerenciamento de Energia

- **Deep Sleep** do ESP32  
- *Power Gating*: desligamento físico dos sensores via GPIO  
- Intervalo de coleta configurável (em minutos)  

---

## 🔘 Modos de Operação (Botão Físico)

| Ação | Duração | Função |
|-----|--------|--------|
| Clique curto | < 3s | Leitura forçada imediata |
| Pressão média | 3–6s | Tara da balança |
| Pressão longa | > 6s | Modo de configuração |

---

## 🌐 Configuração em Campo (Captive Portal)

Quando acionado o modo de configuração, o dispositivo cria uma rede Wi-Fi chamada:

```
Colmeia-Config
```

Através de uma interface web local, o usuário pode configurar:

- SSID da rede Wi-Fi  
- Senha da rede  
- ID único da colmeia  
- Intervalo de leitura (minutos)

As configurações são persistidas na memória não volátil do ESP32 (*Preferences*).

---

## 📡 Comunicação MQTT

Os dados são enviados para um broker MQTT utilizando o seguinte padrão de tópico:

```
hive/<device_id>/sensors
```

### Payload JSON

```json
{
  "weight": "25.350",
  "temp_i": "34.80",
  "humid_i": "65.50",
  "temp_e": "28.12",
  "timestamp": "2025-10-29T15:30:00Z"
}
```

---

## 🧠 Resiliência e Buffer Offline

Caso não seja possível estabelecer conexão com a rede ou com o broker MQTT:

- O payload JSON é salvo localmente no sistema de arquivos **LittleFS**
- Os dados são armazenados individualmente com base no timestamp
- Ao restabelecer a conexão, os dados pendentes são enviados em lotes
- A ordem cronológica é garantida pelo timestamp de cada leitura

---

## 🗂️ Sistema de Arquivos

- Sistema de arquivos interno: **LittleFS**
- Diretório de buffer: `/buffer`
- Limite de envio por lote: configurável (`MAX_FILES_PER_BATCH`)

---

## 🧰 Hardware Utilizado

- ESP32-S3 DevKit  
- HX711 (conversor A/D para célula de carga)  
- Células de carga (balança)  
- Sensor AHT10 (temperatura e umidade internas)  
- Sensor DS18B20 (temperatura externa)  
- RTC DS3231  
- Buzzer passivo  
- Botão físico  

---

## 🧪 Bibliotecas Utilizadas

- PubSubClient  
- HX711  
- Adafruit_AHTX0  
- DallasTemperature  
- OneWire  
- ArduinoJson  
- RTClib  
- WiFiManager  
- Preferences  
- LittleFS  

---

## ⚙️ Configuração do Firmware

```cpp
const char* mqtt_server = "your_mqtt_broker";
const int mqtt_port = 1883;
const char* mqtt_user = "your_mqtt_user";
const char* mqtt_password = "your_mqtt_password";
```

---

## 🏫 Contexto Acadêmico

Este firmware integra um sistema completo de IoT desenvolvido como parte de um **Trabalho de Conclusão de Curso em Engenharia de Computação**, com foco em monitoramento remoto de colmeias, eficiência energética e escalabilidade.
