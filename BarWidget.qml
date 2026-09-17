import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Ui

BarWidget {
  id: root
  moduleName: "rafaelportomoura.avell-control-center"

  readonly property string pluginDir: decodeURIComponent(String(Qt.resolvedUrl(".")).replace(/^file:\/\//, "")).replace(/\/$/, "")
  readonly property string helper: pluginDir + "/bin/avell-ctl"

  // User Settings
  readonly property bool showTempInBar: setting("showTempInBar", true) !== false
  readonly property int pollIntervalSec: Math.max(1, Math.min(60, Number(setting("pollIntervalSec", 3)) || 3))

  // State
  property bool deviceConnected: true
  property bool permissionOk: false
  property bool keyboardOn: true
  property int brightness: 25
  property int brightnessPercent: 50
  property int speed: 5
  property string effectName: "rainbow"
  property int cpuTemp: -1
  property int batteryPercent: -1
  property string batteryStatus: ""
  property string powerProfile: "performance"

  readonly property bool opened: panelLoader.item
    ? panelLoader.item.opened === true
    : false
  readonly property bool popoutSwitchClosing: panelLoader.item
    ? panelLoader.item.popoutSwitchClosing === true
    : false

  function open() {
    if (panelLoader.item) panelLoader.item.open()
  }

  function close() {
    if (panelLoader.item) panelLoader.item.close()
  }

  function toggle() {
    if (panelLoader.item) panelLoader.item.toggle()
  }

  function closeForPopoutSwitch() {
    if (panelLoader.item) panelLoader.item.closeForPopoutSwitch()
  }

  function injectPanel() {
    if (!panelLoader.item) return
    panelLoader.item.bar = root.bar
    panelLoader.item.anchorItem = button
    panelLoader.item.hostWidget = root
    panelLoader.item.helper = root.helper
    panelLoader.item.pluginDir = root.pluginDir
  }

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  onBarChanged: injectPanel()

  function refresh() {
    if (!statusProc.running) statusProc.running = true
  }

  Process {
    id: statusProc
    command: [root.helper, "status"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        try {
          var res = JSON.parse(text || "{}")
          if (res.device) {
            root.deviceConnected = res.device.connected === true
            root.permissionOk = res.device.permission_ok === true
            root.keyboardOn = res.device.is_on === true
            root.brightness = res.device.brightness || 25
            root.brightnessPercent = res.device.brightness_percent || 50
            root.speed = res.device.speed || 5
            root.effectName = res.device.effect_name || "rainbow"
          }
          if (res.system) {
            root.cpuTemp = res.system.cpu_temp_celsius !== undefined ? res.system.cpu_temp_celsius : -1
            root.batteryPercent = res.system.battery_percent !== undefined ? res.system.battery_percent : -1
            root.batteryStatus = res.system.battery_status || ""
            root.powerProfile = res.system.power_profile || "performance"
          }
        } catch (e) {
          // ignore parse errors
        }
      }
    }
  }

  Timer {
    id: pollTimer
    interval: root.pollIntervalSec * 1000
    running: true
    repeat: true
    triggeredOnStart: true
    onTriggered: root.refresh()
  }

  Loader {
    id: panelLoader
    active: true
    source: Qt.resolvedUrl("Panel.qml")
    visible: false
    onLoaded: {
      root.injectPanel()
      Qt.callLater(root.injectPanel)
    }
  }

  WidgetButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: (root.showTempInBar && root.cpuTemp > 0)
      ? ("\uf11c " + root.cpuTemp + "°C")
      : "\uf11c"
    tooltipText: "Avell A60 MUV Control Center"
    onPressed: function(buttonCode) {
      if (buttonCode === Qt.LeftButton) root.toggle()
      else if (buttonCode === Qt.RightButton) root.refresh()
    }
  }
}
