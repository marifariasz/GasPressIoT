# 🌬️ GasPress IoT: Monitoramento Inteligente de Gás e Pressão 🌡️

Bem-vindo ao **GasPress IoT**, um projeto de Internet das Coisas (IoT) desenvolvido para monitoramento em tempo real de pressão e concentração de gás utilizando a placa **BitDogLab/RP2040**. Este sistema integra sensores analógicos, comunicação MQTT e atuadores (LED e buzzer), com um painel no celular para visualização de dados em gráficos e alertas visuais. 🚀

---

## 📋 Descrição do Projeto

O **GasPress IoT** é um sistema IoT que monitora pressão e concentração de gás em ambientes, utilizando um joystick para simular sensores. Ele publica dados em tempo real via protocolo MQTT e controla um LED e um buzzer para alertas. Um painel no celular exibe gráficos separados para pressão e gás, além de um LED indicador virtual que alerta quando os limites são excedidos. Ideal para aplicações em segurança e automação ambiental! 🛠️

---

## 🎯 Funcionalidades

- **Monitoramento de Sensores** 📏:
  - Lê valores de pressão (eixo Y do joystick, pino 26) e gás (eixo X do joystick, pino 27) a cada 2 segundos usando ADC.
  - Converte valores brutos (0–4095) em porcentagens (0–100%) para pressão e gás (escala de tensão 0–3,3V para gás).
- **Comunicação MQTT** 📡:
  - Publica dados nos tópicos `/pressure` e `/gas` com atualizações apenas para variações >0,1%.
  - Publica o estado do LED ("On"/"Off") no tópico `/led`.
  - Subscreve tópicos `/led`, `/print`, `/ping` e `/exit` para controle remoto e funcionalidades adicionais.
- **Controle de Atuadores** 💡:
  - **LED vermelho (pino 13):** Acende se pressão >60% ou gás >40%, ou via comando MQTT no tópico `/led` ("On"/"1" ou "Off"/"0").
  - **Buzzer (pino 21, PWM):** Opera intermitentemente (500 ms ligado/desligado, 1000 Hz, 50% duty cycle) quando o LED está ligado.
- **Painel no Celular** 📱:
  - Interface (app ou web) exibe gráficos em tempo real para pressão e gás, subscrita aos tópicos `/pressure` e `/gas`.
  - Inclui um LED indicador virtual (vermelho quando pressão >60% ou gás >40%, cinza quando abaixo dos limites).
- **Conexão Robusta** 🔗:
  - Conecta-se à rede Wi-Fi (`TIM_ULTRAFIBRA_28A0`) e ao broker MQTT (`192.168.1.9`) com autenticação (`mariana`).
  - Usa tópico de última vontade (`/online`) para indicar status de conexão ("1" ao conectar, "0" ao desconectar).

---

## 🛠️ Hardware Utilizado

- **Placa BitDogLab/RP2040** 🖥️:
  - Microcontrolador RP2040 com módulo Wi-Fi CYW43.
- **Periféricos**:
  - **ADC (Pinos 26 e 27):** Leitura de pressão (eixo Y do joystick) e gás (eixo X do joystick).
  - **GPIO (Pino 13):** LED vermelho externo para alertas visuais.
  - **PWM (Pino 21):** Buzzer ativo para alertas sonoros intermitentes.
  - **Wi-Fi (CYW43):** Comunicação com broker MQTT.
- **Periféricos Não Utilizados**:
  - Bluetooth, botões, display OLED, matriz de LEDs, LED RGB, tratamento de debounce (não aplicável).

---

## 📦 Pré-requisitos

- **Hardware**:
  - Placa BitDogLab/RP2040.
  - Joystick conectado aos pinos 26 (ADC0) e 27 (ADC1).
  - LED vermelho conectado ao pino 13.
  - Buzzer ativo conectado ao pino 21.
- **Software**:
  - [Pico SDK](https://github.com/raspberrypi/pico-sdk) para compilação.
  - Bibliotecas: `pico/stdlib`, `pico/cyw43_arch`, `hardware/gpio`, `hardware/adc`, `hardware/pwm`, `lwip/apps/mqtt`.
  - Broker MQTT (ex.: Mosquitto) rodando em `192.168.1.9`.
  - Rede Wi-Fi configurada (`TIM_ULTRAFIBRA_28A0`, senha `64t4fu76eb`).
- **Painel no Celular**:
  - Aplicativo ou interface web compatível com MQTT (ex.: usando biblioteca `paho-mqtt` para JavaScript).
  - Ferramenta de visualização de gráficos (ex.: Chart.js para gráficos em tempo real).

---

## ⚙️ Configuração

1. **Configurar o Ambiente**:
   - Instale o Pico SDK e configure o ambiente de desenvolvimento (ex.: VS Code com CMake).
   - Clone o repositório ou copie o código para um arquivo `.c`.

2. **Conectar o Hardware**:
   - Conecte o joystick aos pinos 26 (eixo Y) e 27 (eixo X).
   - Conecte o LED ao pino 13 (com resistor apropriado).
   - Conecte o buzzer ao pino 21.

3. **Configurar Credenciais**:
   - Atualize as constantes no código:
     ```c
     #define WIFI_SSID "TIM_ULTRAFIBRA_28A0"
     #define WIFI_PASSWORD "64t4fu76eb"
     #define MQTT_SERVER "192.168.1.9"
     #define MQTT_USERNAME "mariana"
     #define MQTT_PASSWORD "mariana"
     ```

4. **Compilar e Carregar**:
   - Compile o código com o Pico SDK.
   - Carregue o firmware na placa BitDogLab/RP2040 via USB.

5. **Configurar o Painel no Celular**:
   - Implemente uma interface web ou app que subscreva os tópicos `/pressure`, `/gas` e `/led`.
   - Use uma biblioteca como Chart.js para exibir gráficos separados de pressão e gás.
   - Adicione um LED indicador virtual que acende (vermelho) quando pressão >60% ou gás >40%.

---

## 🚀 Como Usar

1. **Iniciar o Sistema**:
   - Ligue a placa BitDogLab/RP2040. Ela se conectará automaticamente à rede Wi-Fi e ao broker MQTT.
   - O sistema começa a ler pressão e gás a cada 2 segundos e publica os dados nos tópicos `/pressure` e `/gas`.

2. **Monitorar Dados**:
   - Acesse o painel no celular para visualizar gráficos em tempo real de pressão e gás.
   - O LED indicador virtual no painel acende (vermelho) se pressão >60% ou gás >40%, e fica cinza caso contrário.

3. **Controlar o LED**:
   - Envie comandos MQTT para o tópico `/led`:
     - `"On"` ou `"1"`: Liga o LED e o buzzer (intermitente).
     - `"Off"` ou `"0"`: Desliga o LED e o buzzer.
   - O LED físico e o buzzer também são ativados automaticamente com base nos limites de pressão e gás.

4. **Funcionalidades Adicionais**:
   - **Tópico `/ping`**: Envie uma mensagem para receber o tempo de atividade no tópico `/uptime`.
   - **Tópico `/print`**: Envie mensagens para exibir no console da placa.
   - **Tópico `/exit`**: Envie para desconectar o cliente MQTT.

---

## 📊 Exemplo de Saída

- **Console (depuração)**:
  ```
  mqtt client starting
  Device name: pico1234
  Connected to Wifi
  Connected to MQTT broker
  Pressure: Raw ADC=2048, Percent=50.00%
  Gas: Raw ADC=2048, Voltage=1.650V, Gas=165.00, Percent=50.00%
  Setting LED on pin 13 to On (actual pin state: 1)
  Buzzer PWM on
  Published LED On to /led
  ```

- **Painel no Celular**:
  - Gráfico de pressão: Linha mostrando valores de 0–100% atualizados a cada 2 segundos.
  - Gráfico de gás: Linha mostrando valores de 0–100% atualizados a cada 2 segundos.
  - LED indicador: Vermelho quando pressão >60% ou gás >40%, cinza caso contrário.

---

## 🔍 Notas Técnicas

- **Código Base**: Adaptado de [pico-examples](https://github.com/raspberrypi/pico-examples/tree/master/pico_w/wifi/mqtt).
- **Limites de Alerta**:
  - Pressão: >60% (LED e buzzer ativados).
  - Gás: >40% (LED e buzzer ativados na função `publish_pressure`), >50% (na função `publish_gas`).
- **Otimização**: Publicações MQTT só ocorrem para variações >0,1%, reduzindo tráfego de rede.
- **Segurança**: Conexão MQTT com autenticação (`mariana`), mas sem TLS (configuração opcional no código).

---

## 🛠️ Possíveis Melhorias

- Adicionar suporte a TLS para maior segurança na comunicação MQTT. 🔒
- Implementar um display OLED na placa para visualização local dos dados. 📺
- Integrar sensores reais de pressão e gás em vez de simulação com joystick. 🌡️
- Expandir o painel no celular com controles interativos para ajustar limites de alerta. 📲

---

## 📝 Licença

Este projeto é de código aberto e está licenciado sob a [MIT License](LICENSE). Sinta-se à vontade para usar, modificar e compartilhar! 😊

---

**Desenvolvido por Mariana Farias da Silva**  
**AULA IoT - Ricardo Prates - 001**  
**Data: 27/05/2025**  
🌟 **Monitore o ambiente com GasPress IoT!** 🌟