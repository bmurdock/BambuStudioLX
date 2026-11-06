export function arcPath(
  cx: number, cy: number,
  r: number,
  startDeg: number, endDeg: number,
  clockwise = true,
) {
  const rad = (a: number) => (Math.PI / 180) * a;
  const toXY = (deg: number) => [cx + r * Math.cos(rad(deg)), cy + r * Math.sin(rad(deg))] as const;
  const [x1, y1] = toXY(startDeg);
  const [x2, y2] = toXY(endDeg);
  let delta = ((endDeg - startDeg) % 360 + 360) % 360;
  if (!clockwise) delta = 360 - delta;
  return `M ${x1} ${y1} A ${r} ${r} 0 ${delta > 180 ? 1 : 0} ${clockwise ? 1 : 0} ${x2} ${y2}`;
}
