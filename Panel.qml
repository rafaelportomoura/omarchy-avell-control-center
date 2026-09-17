import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Ui
import "AvellModel.js" as Avell

Panel {
  id: root
  moduleName: "rafaelportomoura.avell-control-center"
  manageIpc: false

  property var anchorItem: null
  property var hostWidget: null
  property string helper: ""
  property string pluginDir: ""

  property string modeTab: "colors" // "colors" or "effects"
  property string activeEffect: hostWidget ? hostWidget.effectName : "rainbow"
  property int effectSpeed: 5
  property string effectColor: "random"
  property string effectDirection: "right"
  property string saveFeedback: ""

  function open() {
    root.controller.show()
    if (root.hostWidget) root.hostWidget.refresh()
  }

  function close() {
    root.controller.hide()
  }

  function toggle() {
    root.opened ? close() : open()
  }

  function switchPanel(direction) {
    if (root.bar && typeof root.bar.switchPanelFrom === "function")
      return root.bar.switchPanelFrom(root.hostWidget || root, direction)
    return false
  }

  // Execution helper for commands
  function execCommand(args) {
    if (actionProc.running) {
      actionProc.terminate()
    }
    actionProc.command = args
    actionProc.running = true
  }

  function setPowerProfile(profile) {
    execCommand([root.helper, "profile", profile])
    if (root.hostWidget) {
      root.hostWidget.powerProfile = profile
    }
  }

  function toggleKeyboard() {
    if (!root.hostWidget) return
    var target = !root.hostWidget.keyboardOn
    root.hostWidget.keyboardOn = target
    execCommand([root.helper, target ? "on" : "off"])
  }

  Timer {
    id: brightnessDebounce
    interval: 150
    repeat: false
    property int targetBrightness: 25
    onTriggered: {
      root.execCommand([root.helper, "brightness", String(targetBrightness)])
    }
  }

  function setBrightness(val) {
    if (!root.hostWidget) return
    root.hostWidget.brightness = val
    root.hostWidget.brightnessPercent = Math.round((val * 100) / 50)
    brightnessDebounce.targetBrightness = val
    brightnessDebounce.restart()
  }

  function setColor(colorHex) {
    var b = root.hostWidget ? root.hostWidget.brightness : 36
    execCommand([root.helper, "color", colorHex, "--brightness", String(b)])
    if (root.hostWidget) {
      root.hostWidget.keyboardOn = true
      root.hostWidget.effectName = "user"
    }
  }

  function applyEffect(name) {
    root.activeEffect = name
    var b = root.hostWidget ? root.hostWidget.brightness : 36
    var args = [root.helper, "effect", name, "--speed", String(root.effectSpeed), "--brightness", String(b)]
    if (root.effectColor !== "random") {
      args.push("--color")
      args.push(root.effectColor)
    }
    if (name === "wave") {
      args.push("--direction")
      args.push(root.effectDirection)
    }
    execCommand(args)
    if (root.hostWidget) {
      root.hostWidget.keyboardOn = true
      root.hostWidget.effectName = name
    }
  }

  function saveToRom() {
    execCommand([root.helper, "save"])
    root.saveFeedback = "Configurações salvas no chip (ROM) com sucesso!"
    feedbackTimer.restart()
  }

  function installUdevRule() {
    var scriptPath = root.pluginDir + "/scripts/setup-udev.sh"
    execCommand(["pkexec", scriptPath])
    checkPermsTimer.restart()
  }

  Process {
    id: actionProc
  }

  Timer {
    id: feedbackTimer
    interval: 3500
    onTriggered: root.saveFeedback = ""
  }

  Timer {
    id: checkPermsTimer
    interval: 2500
    onTriggered: {
      if (root.hostWidget) root.hostWidget.refresh()
    }
  }

  KeyboardPanel {
    id: panel
    anchorItem: root.anchorItem
    owner: root.hostWidget || root
    bar: root.bar
    open: root.opened
    focusTarget: keyCatcher
    contentWidth: panel.fittedContentWidth(Style.space(400))
    contentHeight: panel.fittedContentHeight(contentCol.implicitHeight + Style.space(24))

    PanelKeyCatcher {
      id: keyCatcher
      anchors.fill: parent
      onCloseRequested: root.close()
      onTabRequested: function(direction) { root.switchPanel(direction) }

      Column {
        id: contentCol
        width: parent.width
        spacing: Style.space(12)
        topPadding: Style.space(10)
        bottomPadding: Style.space(10)
        leftPadding: Style.space(14)
        rightPadding: Style.space(14)

        // -------------------------------------------------------------
        // Header
        // -------------------------------------------------------------
        Row {
          width: parent.width - Style.space(28)
          spacing: Style.space(10)

          Rectangle {
            width: Style.space(36)
            height: Style.space(36)
            radius: Style.space(8)
            color: Style.selectedFillFor(Color.foreground, Color.accent)

            Text {
              anchors.centerIn: parent
              text: "\uf11c"
              font.family: Style.font.family
              font.pixelSize: Style.font.title
              color: Color.accent
            }
          }

          Column {
            width: parent.width - Style.space(46)
            spacing: Style.space(2)

            Text {
              text: "Avell A60 MUV"
              color: Color.foreground
              font.family: Style.font.family
              font.pixelSize: Style.font.subtitle
              font.bold: true
            }

            Text {
              text: "Controle de Teclado & Desempenho"
              color: Qt.darker(Color.foreground, 1.4)
              font.family: Style.font.family
              font.pixelSize: Style.font.caption
            }
          }
        }

        // -------------------------------------------------------------
        // Permission Warning Alert (Visible only if permission_ok is false)
        // -------------------------------------------------------------
        Rectangle {
          visible: root.hostWidget ? !root.hostWidget.permissionOk : false
          width: parent.width - Style.space(28)
          height: permCol.implicitHeight + Style.space(16)
          radius: Style.space(8)
          color: Qt.rgba(Color.urgent.r, Color.urgent.g, Color.urgent.b, 0.12)
          border.color: Color.urgent
          border.width: 1

          Column {
            id: permCol
            anchors.fill: parent
            anchors.margins: Style.space(10)
            spacing: Style.space(6)

            Row {
              spacing: Style.space(8)
              Text {
                text: "\uf071"
                font.family: Style.font.family
                color: Color.urgent
                font.bold: true
              }
              Text {
                text: "Permissão USB Necessária"
                color: Color.urgent
                font.family: Style.font.family
                font.bold: true
              }
            }

            Text {
              width: parent.width
              wrapMode: Text.WordWrap
              text: "O acesso direto ao controlador ITE 8291 requer uma regra udev. Clique abaixo para configurar com privilégios de administrador."
              color: Color.foreground
              font.family: Style.font.family
              font.pixelSize: Style.font.bodySmall
            }

            Button {
              text: "🔑 Instalar Regra udev"
              bordered: true
              accent: Color.urgent
              onClicked: root.installUdevRule()
            }
          }
        }

        // -------------------------------------------------------------
        // Telemetry Badges
        // -------------------------------------------------------------
        Row {
          width: parent.width - Style.space(28)
          spacing: Style.space(8)

          // CPU Temp Badge
          Rectangle {
            width: (parent.width - Style.space(16)) / 3
            height: Style.space(38)
            radius: Style.space(6)
            color: Qt.rgba(Color.foreground.r, Color.foreground.g, Color.foreground.b, 0.06)
            border.color: Qt.rgba(Color.foreground.r, Color.foreground.g, Color.foreground.b, 0.12)

            Row {
              anchors.centerIn: parent
              spacing: Style.space(6)
              Text {
                text: "\uf2c9"
                font.family: Style.font.family
                color: (root.hostWidget && root.hostWidget.cpuTemp > 80) ? Color.urgent : Color.accent
              }
              Text {
                text: (root.hostWidget && root.hostWidget.cpuTemp > 0) ? (root.hostWidget.cpuTemp + "°C") : "--"
                color: Color.foreground
                font.family: Style.font.family
                font.bold: true
                font.pixelSize: Style.font.bodySmall
              }
            }
          }

          // Battery Badge
          Rectangle {
            width: (parent.width - Style.space(16)) / 3
            height: Style.space(38)
            radius: Style.space(6)
            color: Qt.rgba(Color.foreground.r, Color.foreground.g, Color.foreground.b, 0.06)
            border.color: Qt.rgba(Color.foreground.r, Color.foreground.g, Color.foreground.b, 0.12)

            Row {
              anchors.centerIn: parent
              spacing: Style.space(6)
              Text {
                text: "\uf240"
                font.family: Style.font.family
                color: Color.accent
              }
              Text {
                text: (root.hostWidget && root.hostWidget.batteryPercent >= 0) ? (root.hostWidget.batteryPercent + "%") : "--"
                color: Color.foreground
                font.family: Style.font.family
                font.bold: true
                font.pixelSize: Style.font.bodySmall
              }
            }
          }

          // Power Profile Badge
          Rectangle {
            width: (parent.width - Style.space(16)) / 3
            height: Style.space(38)
            radius: Style.space(6)
            color: Qt.rgba(Color.foreground.r, Color.foreground.g, Color.foreground.b, 0.06)
            border.color: Qt.rgba(Color.foreground.r, Color.foreground.g, Color.foreground.b, 0.12)

            Row {
              anchors.centerIn: parent
              spacing: Style.space(6)
              Text {
                text: root.hostWidget && root.hostWidget.powerProfile === "performance" ? "\uf0e7" : (root.hostWidget && root.hostWidget.powerProfile === "power-saver" ? "\uf06c" : "\uf24e")
                font.family: Style.font.family
                color: Color.accent
              }
              Text {
                text: root.hostWidget && root.hostWidget.powerProfile === "performance" ? "Turbo" : (root.hostWidget && root.hostWidget.powerProfile === "power-saver" ? "Eco" : "Equil.")
                color: Color.foreground
                font.family: Style.font.family
                font.bold: true
                font.pixelSize: Style.font.bodySmall
              }
            }
          }
        }

        PanelSeparator { width: parent.width - Style.space(28) }

        // -------------------------------------------------------------
        // Power Profiles Section
        // -------------------------------------------------------------
        PanelSectionHeader { text: "PERFIL DE ENERGIA & DESEMPENHO" }

        Row {
          width: parent.width - Style.space(28)
          spacing: Style.space(8)

          Button {
            width: (parent.width - Style.space(16)) / 3
            text: "Turbo"
            iconText: "\uf0e7"
            tooltipText: "Modo Alto Desempenho"
            selected: root.hostWidget ? root.hostWidget.powerProfile === "performance" : false
            onClicked: root.setPowerProfile("performance")
          }

          Button {
            width: (parent.width - Style.space(16)) / 3
            text: "Equilibrado"
            iconText: "\uf24e"
            tooltipText: "Modo Balanceado"
            selected: root.hostWidget ? root.hostWidget.powerProfile === "balanced" : true
            onClicked: root.setPowerProfile("balanced")
          }

          Button {
            width: (parent.width - Style.space(16)) / 3
            text: "Econômico"
            iconText: "\uf06c"
            tooltipText: "Modo Silencioso / Poupança"
            selected: root.hostWidget ? root.hostWidget.powerProfile === "power-saver" : false
            onClicked: root.setPowerProfile("power-saver")
          }
        }

        PanelSeparator { width: parent.width - Style.space(28) }

        // -------------------------------------------------------------
        // Keyboard RGB Section
        // -------------------------------------------------------------
        Row {
          width: parent.width - Style.space(28)

          PanelSectionHeader {
            text: "ILUMINAÇÃO DO TECLADO"
            anchors.verticalCenter: parent.verticalCenter
          }

          Item {
            width: parent.width - childrenRect.width - Style.space(180)
            height: 1
          }

          ToggleSwitch {
            anchors.verticalCenter: parent.verticalCenter
            checked: root.hostWidget ? root.hostWidget.keyboardOn : true
            onToggled: root.toggleKeyboard()
          }
        }

        // Keyboard Controls
        Column {
          visible: root.hostWidget ? root.hostWidget.keyboardOn : true
          width: parent.width - Style.space(28)
          spacing: Style.space(10)

          // Brightness Control
          Row {
            width: parent.width
            Text {
              text: "Brilho do Teclado"
              color: Color.foreground
              font.family: Style.font.family
              font.pixelSize: Style.font.bodySmall
              anchors.verticalCenter: parent.verticalCenter
            }
            Item { width: parent.width - Style.space(150); height: 1 }
            Text {
              text: (root.hostWidget ? root.hostWidget.brightnessPercent : 50) + "%"
              color: Color.accent
              font.family: Style.font.family
              font.bold: true
              font.pixelSize: Style.font.bodySmall
              anchors.verticalCenter: parent.verticalCenter
            }
          }

          PanelSlider {
            width: parent.width
            bar: root.bar
            minimum: 0
            maximum: 50
            step: 1
            integer: true
            value: root.hostWidget ? root.hostWidget.brightness : 25
            onMoved: function(v) { root.setBrightness(Math.round(v)) }
            onReleased: function(v) { root.setBrightness(Math.round(v)) }
          }

          // Tabs: Cores vs Efeitos
          Row {
            width: parent.width
            spacing: Style.space(8)

            Button {
              width: (parent.width - Style.space(8)) / 2
              text: "Cores Estáticas"
              iconText: "\uf042"
              selected: root.modeTab === "colors"
              onClicked: root.modeTab = "colors"
            }

            Button {
              width: (parent.width - Style.space(8)) / 2
              text: "Efeitos Dinâmicos"
              iconText: "\uf0d0"
              selected: root.modeTab === "effects"
              onClicked: root.modeTab = "effects"
            }
          }

          // Tab 1: Static Colors Palette
          Column {
            visible: root.modeTab === "colors"
            width: parent.width
            spacing: Style.space(8)

            Flow {
              width: parent.width
              spacing: Style.space(8)

              Repeater {
                model: Avell.COLOR_PALETTE
                delegate: Rectangle {
                  width: Style.space(32)
                  height: Style.space(32)
                  radius: Style.space(6)
                  color: modelData.hex
                  border.color: Color.foreground
                  border.width: 1

                  MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.setColor(modelData.hex)
                  }

                  PanelToolTip {
                    text: modelData.label
                  }
                }
              }
            }

            // Custom Hex Input
            Row {
              width: parent.width
              spacing: Style.space(8)

              TextField {
                id: customHexInput
                width: parent.width - Style.space(90)
                placeholderText: "#FF0055 ou nome"
                text: ""
                onAccepted: {
                  if (customHexInput.text.trim().length > 0) {
                    root.setColor(customHexInput.text.trim())
                  }
                }
              }

              Button {
                width: Style.space(82)
                text: "Aplicar"
                onClicked: {
                  if (customHexInput.text.trim().length > 0) {
                    root.setColor(customHexInput.text.trim())
                  }
                }
              }
            }
          }

          // Tab 2: Dynamic Effects
          Column {
            visible: root.modeTab === "effects"
            width: parent.width
            spacing: Style.space(8)

            Flow {
              width: parent.width
              spacing: Style.space(6)

              Repeater {
                model: Avell.EFFECTS
                delegate: Button {
                  text: modelData.label
                  iconText: modelData.icon
                  selected: root.activeEffect === modelData.id
                  onClicked: root.applyEffect(modelData.id)
                }
              }
            }

            // Wave Direction Controls
            Row {
              visible: root.activeEffect === "wave"
              width: parent.width
              spacing: Style.space(6)

              Text {
                text: "Direção:"
                color: Color.foreground
                font.family: Style.font.family
                font.pixelSize: Style.font.bodySmall
                anchors.verticalCenter: parent.verticalCenter
              }

              Button {
                text: "➡"
                tooltipText: "Direita"
                selected: root.effectDirection === "right"
                onClicked: { root.effectDirection = "right"; root.applyEffect("wave"); }
              }
              Button {
                text: "⬅"
                tooltipText: "Esquerda"
                selected: root.effectDirection === "left"
                onClicked: { root.effectDirection = "left"; root.applyEffect("wave"); }
              }
              Button {
                text: "⬆"
                tooltipText: "Cima"
                selected: root.effectDirection === "up"
                onClicked: { root.effectDirection = "up"; root.applyEffect("wave"); }
              }
              Button {
                text: "⬇"
                tooltipText: "Baixo"
                selected: root.effectDirection === "down"
                onClicked: { root.effectDirection = "down"; root.applyEffect("wave"); }
              }
            }

            // Speed Control
            Row {
              width: parent.width
              spacing: Style.space(8)

              Text {
                text: "Velocidade do Efeito"
                color: Color.foreground
                font.family: Style.font.family
                font.pixelSize: Style.font.bodySmall
                anchors.verticalCenter: parent.verticalCenter
              }

              Item { width: parent.width - Style.space(190); height: 1 }

              Text {
                text: "Nível " + root.effectSpeed
                color: Color.accent
                font.family: Style.font.family
                font.bold: true
                font.pixelSize: Style.font.bodySmall
                anchors.verticalCenter: parent.verticalCenter
              }
            }

            PanelSlider {
              width: parent.width
              bar: root.bar
              minimum: 1
              maximum: 10
              step: 1
              integer: true
              value: root.effectSpeed
              onMoved: function(v) {
                root.effectSpeed = Math.round(v)
                root.applyEffect(root.activeEffect)
              }
            }
          }
        }

        PanelSeparator { width: parent.width - Style.space(28) }

        // -------------------------------------------------------------
        // Bottom Actions & Save to Hardware ROM
        // -------------------------------------------------------------
        Row {
          width: parent.width - Style.space(28)
          spacing: Style.space(8)

          Button {
            width: parent.width
            text: "💾 Gravar Configurações no Teclado (ROM)"
            tooltipText: "Persiste as cores e efeitos mesmo após reiniciar o laptop"
            onClicked: root.saveToRom()
          }
        }

        Text {
          visible: root.saveFeedback.length > 0
          width: parent.width - Style.space(28)
          text: root.saveFeedback
          color: Color.accent
          font.family: Style.font.family
          font.bold: true
          font.pixelSize: Style.font.bodySmall
          horizontalAlignment: Text.AlignHCenter
        }
      }
    }
  }
}
