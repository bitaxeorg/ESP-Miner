import { HeatmapLightnessPipe } from './heatmap-lightness.pipe';

describe('HeatmapLightnessPipe', () => {
  let pipe: HeatmapLightnessPipe;

  beforeEach(() => {
    pipe = new HeatmapLightnessPipe();
  });

  it('create an instance', () => {
    expect(pipe).toBeTruthy();
  });

  it('should calculate correct lightness for perfect ratio', () => {
    // 500 GH/s domain, 500 GH/s expected, 1 ASIC, 1 domain -> ratio = 1
    const lightness = pipe.transform(500, 500, 1, 1);
    expect(lightness).toBe('0.500');
  });

  it('should handle zero or missing expected hashrate safely', () => {
    const lightness = pipe.transform(100, 0, 1, 1);
    expect(parseFloat(lightness)).toBeGreaterThanOrEqual(0);
  });
});
