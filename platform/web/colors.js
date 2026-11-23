function channelLuminance(hex, offset) {
  const value = parseInt(hex.slice(offset, offset + 2), 16) / 255;
  return value <= 0.04045 ? value / 12.92 : ((value + 0.055) / 1.055) ** 2.4;
}

export function contrastForeground(hex) {
  const luminance = 0.2126 * channelLuminance(hex, 1) +
    0.7152 * channelLuminance(hex, 3) +
    0.0722 * channelLuminance(hex, 5);
  const black = (luminance + 0.05) / 0.05;
  const white = 1.05 / (luminance + 0.05);
  return black >= white ? "#000000" : "#FFFFFF";
}

export function effectiveColors(control, theme = {}) {
  const background = control.backgroundColor ?? theme.backgroundColor;
  const explicit = control.foregroundColor ?? theme.foregroundColor;
  const foreground = explicit ?? (background ? contrastForeground(background) : undefined);
  return { background, foreground };
}

export function chromeColors(windowDefinition) {
  const background = windowDefinition.backgroundColor;
  const explicit = windowDefinition.foregroundColor;
  const foreground = explicit ?? (background ? contrastForeground(background) : undefined);
  return { background, foreground };
}
