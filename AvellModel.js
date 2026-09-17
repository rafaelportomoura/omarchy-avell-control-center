.pragma library

var COLOR_PALETTE = [
  { name: "red", hex: "#FF0000", label: "Vermelho" },
  { name: "orange", hex: "#FF6600", label: "Laranja" },
  { name: "yellow", hex: "#FFCC00", label: "Amarelo" },
  { name: "green", hex: "#00FF00", label: "Verde" },
  { name: "teal", hex: "#00FFFF", label: "Ciano" },
  { name: "blue", hex: "#0066FF", label: "Azul" },
  { name: "purple", hex: "#CC00FF", label: "Roxo" },
  { name: "pink", hex: "#FF007F", label: "Rosa" },
  { name: "white", hex: "#FFFFFF", label: "Branco" },
  { name: "gold", hex: "#FFD700", label: "Dourado" }
];

var EFFECTS = [
  {
    id: "rainbow",
    name: "rainbow",
    label: "Arco-íris",
    icon: "\uf043", // tint / rainbow
    supportsColor: false,
    supportsSpeed: false,
    supportsDirection: false
  },
  {
    id: "wave",
    name: "wave",
    label: "Onda",
    icon: "\uf0c9", // bars / wave
    supportsColor: false,
    supportsSpeed: true,
    supportsDirection: true
  },
  {
    id: "breathing",
    name: "breathing",
    label: "Respiração",
    icon: "\uf004", // heart / pulse
    supportsColor: true,
    supportsSpeed: true,
    supportsDirection: false
  },
  {
    id: "ripple",
    name: "ripple",
    label: "Ondulação",
    icon: "\uf192", // dot circle
    supportsColor: true,
    supportsSpeed: true,
    supportsDirection: false
  },
  {
    id: "raindrop",
    name: "raindrop",
    label: "Chuva",
    icon: "\uf0e9", // umbrella
    supportsColor: true,
    supportsSpeed: true,
    supportsDirection: false
  },
  {
    id: "aurora",
    name: "aurora",
    label: "Aurora",
    icon: "\uf185", // sun
    supportsColor: true,
    supportsSpeed: true,
    supportsDirection: false
  },
  {
    id: "fireworks",
    name: "fireworks",
    label: "Fogos",
    icon: "\uf135", // rocket
    supportsColor: true,
    supportsSpeed: true,
    supportsDirection: false
  },
  {
    id: "marquee",
    name: "marquee",
    label: "Letreiro",
    icon: "\uf061", // arrow
    supportsColor: false,
    supportsSpeed: true,
    supportsDirection: false
  },
  {
    id: "random",
    name: "random",
    label: "Aleatório",
    icon: "\uf074", // random
    supportsColor: true,
    supportsSpeed: true,
    supportsDirection: false
  }
];

function effectById(id) {
  for (var i = 0; i < EFFECTS.length; i++) {
    if (EFFECTS[i].id === id) return EFFECTS[i];
  }
  return null;
}
