# Avell Control Center — Omarchy Plugin

Plugin nativo para o **Omarchy (Quattro Shell)** projetado sob medida para o notebook **Avell A60 MUV** (e barebones compatíveis Tongfang / Uniwill com controlador de teclado **ITE Device 8291 Rev 0.03**).

O plugin oferece controle completo da iluminação RGB do teclado por hardware, ajuste de brilho, aplicação de efeitos visuais dinâmicos, persistência na ROM do teclado, controle dos perfis de energia da máquina (Turbo / Equilibrado / Econômico) e telemetria em tempo real.

---

## 💻 Compatibilidade

- **Notebook:** Avell A60 MUV (Barebone Tongfang / Uniwill)
- **Controlador RGB:** ITE Tech. Inc. ITE Device(8291) Rev 0.03 (`048d:ce00`)
- **Sistema Operacional:** Arch Linux / Omarchy com Quickshell (Qt 6)
- **Driver de Plataforma:** `uniwill_laptop` e `power-profiles-daemon`

---

## ✨ Funcionalidades

### 🌈 Teclado RGB (ITE 8291)
- **Interruptor Geral:** Ligar / Desligar luz de fundo do teclado.
- **Controle de Brilho:** Slider contínuo de 0% a 100% (mapeado para os 50 níveis do hardware ITE).
- **Cores Estáticas:** Paleta rápida com 10 cores predefinidas (Vermelho, Laranja, Amarelo, Verde, Ciano, Azul, Roxo, Rosa, Branco, Dourado) + suporte a código hexadecimal `#RRGGBB` personalizado.
- **Efeitos Dinâmicos de Iluminação:**
  - 🌈 **Arco-íris (Rainbow)**
  - 🌊 **Onda (Wave)** com seletor de direção (Direita, Esquerda, Cima, Baixo)
  - 🫁 **Respiração (Breathing)** com controle de velocidade
  - 💧 **Chuva (Raindrop)**
  - 🌌 **Aurora Boreal**
  - 🎆 **Fogos de Artifício (Fireworks)**
  - 💡 **Ondulação (Ripple)**
  - 🏃 **Letreiro (Marquee)**
  - 🎲 **Aleatório (Random)**
- **Controle de Velocidade:** Ajuste fino de 1 a 10 para efeitos animados.
- **Gravação em ROM do Hardware:** Botão para salvar o estado atual diretamente no chip do teclado, mantendo suas preferências mesmo após reiniciar ou desligar o laptop.

### ⚡ Perfis de Desempenho do Sistema
- **Turbo / Performance:** Foco em máxima frequência e potência para jogos e compilação.
- **Equilibrado (Balanced):** Modo balanceado para uso diário.
- **Silencioso / Econômico (Power Saver):** Redução de consumo e rotação de fans para maior autonomia da bateria.

### 📊 Telemetria e Monitoramento
- Temperatura da CPU em tempo real (°C), com exibição opcional diretamente na barra superior do Omarchy.
- Nível de carga e status da bateria (Carregando, Completa, Descarregando).
- Indicador visual do perfil de energia ativo.

---

## 🚀 Instalação Rápida

### 1. Requisitos de Compilação
O utilitário de controle `avell-ctl` foi escrito em C nativo com `libusb-1.0` para máxima performance (latência inferior a 2ms, essencial para sliders responsivos sem travamentos no shell):

```bash
# Certifique-se de que gcc e libusb estejam instalados:
sudo pacman -S --needed gcc libusb pkgconf
```

### 2. Configurar Permissões USB (Regra udev)
Por padrão, o Linux restringe o acesso direto a dispositivos USB HID brutos apenas para o `root`. Para permitir que o Omarchy e o seu usuário controlem o teclado sem solicitar senha a cada ação:

```bash
./scripts/setup-udev.sh
```
*O script solicitará autorização via `pkexec` (janela nativa do polkit no Omarchy) ou `sudo`, criando a regra `/etc/udev/rules.d/99-avell-keyboard.rules` e recarregando o subsistema udev.*

### 3. Compilar e Instalar o Plugin
Execute o script de instalação ou utilize o `Makefile`:

```bash
./scripts/install.sh
# ou
make install
```

O plugin será compilado, validado através do `omarchy plugin validate` e instalado em:
`~/.config/omarchy/plugins/rafaelportomoura.avell-control-center`

### 4. Ativar na Barra do Omarchy
Ative o widget na barra com o comando:

```bash
omarchy plugin enable rafaelportomoura.avell-control-center right
```

### 5. Desinstalação e Remoção
Para desativar e remover o plugin:

```bash
# Desativar o widget da barra
omarchy plugin disable rafaelportomoura.avell-control-center

# Remover o plugin instalado
omarchy plugin remove rafaelportomoura.avell-control-center --yes
```

---

## 🛠️ CLI Independente (`bin/avell-ctl`)

O binário `avell-ctl` também pode ser utilizado diretamente no terminal ou integrado aos seus próprios scripts de atalho:

```bash
# Consultar status completo (formato JSON ou texto)
avell-ctl status

# Ligar / Desligar o teclado
avell-ctl on
avell-ctl off

# Ajustar brilho (0 a 50)
avell-ctl brightness 35

# Definir cor estática (nome ou hex)
avell-ctl color "#00FFCC"
avell-ctl color red --brightness 50

# Aplicar efeitos dinâmicos
avell-ctl effect wave --speed 8 --direction right
avell-ctl effect breathing --speed 4 --color purple
avell-ctl effect rainbow --brightness 40

# Gravar configurações no chip do teclado (persistente ao reiniciar)
avell-ctl save

# Alterar perfil de energia do sistema
avell-ctl profile performance
avell-ctl profile balanced
avell-ctl profile power-saver
```

---

## 📐 Estrutura do Projeto

```
omarchy-avell-control-center/
├── manifest.json         # Manifesto compatível com o padrão Omarchy Quattro
├── BarWidget.qml         # Componente do widget exibido na barra superior
├── Panel.qml             # Painel popover completo com controles táteis
├── AvellModel.js         # Mapeamento de cores, efeitos e utilitários
├── Makefile              # Automação de compilação e instalação
├── bin/
│   ├── avell-ctl.c       # Driver userspace em C (libusb-1.0 + sysfs)
│   └── avell-ctl         # Binário compilado de alta performance
└── scripts/
    ├── install.sh        # Script automatizado de deploy para o Omarchy
    └── setup-udev.sh     # Instalador da regra udev de acesso ao hardware
```

---

## 📜 Licença

Distribuído sob a licença MIT. Baseado nos conhecimentos de engenharia reversa do protocolo ITE 8291 do [avell-unofficial-control-center](https://github.com/rodgomesc/avell-unofficial-control-center) e [ite8291r3-ctl](https://github.com/pobrn/ite8291r3-ctl).
