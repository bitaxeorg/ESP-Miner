import { isFrequencyLow } from './frequency-warning';

describe('isFrequencyLow', () => {
  // Device presets, not a lookup used by production code. BM1370 alone does not
  // distinguish Gamma, Gamma Duo, Gamma Hex, and NerdQAxe++ operating points.
  const devices = [
    { name: 'Max', defaultFrequency: 425, options: [400, 425, 450, 475, 485, 500, 525, 550, 575, 600] },
    { name: 'Ultra / Hex', defaultFrequency: 485, options: [400, 425, 450, 475, 485, 500, 525, 550, 575] },
    { name: 'Supra / Supra Hex', defaultFrequency: 490, options: [400, 425, 450, 475, 485, 490, 500, 525, 550, 575] },
    { name: 'Gamma / GT', defaultFrequency: 525, options: [400, 490, 525, 550, 600, 625, 690] },
    { name: 'Gamma Duo', defaultFrequency: 400, options: [350, 375, 380, 400, 410] },
    { name: 'Gamma Hex', defaultFrequency: 690, options: [400, 490, 525, 550, 600, 625, 690] },
    { name: 'Naja Duo', defaultFrequency: 327, options: [327, 350, 375, 380, 400, 410] },
    { name: 'NerdQAxe+', defaultFrequency: 490, options: [400, 425, 450, 475, 490, 500, 525, 550, 575] },
    { name: 'NerdQAxe++', defaultFrequency: 600, options: [500, 515, 525, 550, 575, 590, 600] }
  ];

  for (const device of devices) {
    it(`accepts the default and all presets for ${device.name}`, () => {
      for (const frequency of [device.defaultFrequency, ...device.options]) {
        expect(isFrequencyLow(frequency, device.options)).withContext(`${frequency} MHz`).toBeFalse();
      }
    });

    it(`warns below the lowest preset for ${device.name}`, () => {
      expect(isFrequencyLow(Math.min(...device.options) - 0.5, device.options)).toBeTrue();
    });
  }

  it('accepts custom frequencies between presets and above the preset range', () => {
    expect(isFrequencyLow(333.3, [327, 350, 410])).toBeFalse();
    expect(isFrequencyLow(600, [327, 350, 410])).toBeFalse();
  });

  it('ignores invalid options and does not assume sorted presets', () => {
    const options = [410, null, 0, -1, NaN, Infinity, '100', 327, 350];
    expect(isFrequencyLow(327, options)).toBeFalse();
    expect(isFrequencyLow(326, options)).toBeTrue();
  });

  it('does not guess a threshold for missing, malformed, or empty metadata', () => {
    for (const options of [undefined, null, [], {}, '400', [0, -1, null, NaN, Infinity, '327']]) {
      expect(isFrequencyLow(327, options)).toBeFalse();
    }
  });

  it('warns about missing or invalid frequency even without metadata', () => {
    for (const frequency of [undefined, null, 0, -1, NaN, Infinity, '327']) {
      expect(isFrequencyLow(frequency, undefined)).toBeTrue();
      expect(isFrequencyLow(frequency, [327, 350])).toBeTrue();
    }
  });
});
