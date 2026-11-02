/** Compare the configured frequency with this device's presets, not its ASIC name.
 * Presets are advisory: they are not hardware limits, and custom values are allowed.
 */
export function isFrequencyLow(frequency: unknown, frequencyOptions: unknown): boolean {
  if (typeof frequency !== 'number' || !Number.isFinite(frequency) || frequency <= 0) {
    return true;
  }

  // Older/fork firmware may omit settings. Do not invent a minimum in that case.
  if (!Array.isArray(frequencyOptions)) return false;

  let minimum = Infinity;
  for (const option of frequencyOptions) {
    if (typeof option === 'number' && Number.isFinite(option) && option > 0) {
      minimum = Math.min(minimum, option);
    }
  }
  return Number.isFinite(minimum) && frequency < minimum;
}
