import { isFrequencyLow } from './common-functions';

describe('isFrequencyLow', () => {
  it('warns below the lowest supplied preset', () => {
    expect(isFrequencyLow(99, [200, 100, 150])).toBeTrue();
  });

  it('accepts frequencies at or above the lowest supplied preset', () => {
    for (const frequency of [100, 125, 200, 250]) {
      expect(isFrequencyLow(frequency, [200, 100, 150])).toBeFalse();
    }
  });

  it('does not guess a threshold for missing, malformed, or empty metadata', () => {
    for (const options of [undefined, null, [], {}, '400', [0, -1, null, NaN, Infinity, '100']]) {
      expect(isFrequencyLow(100, options)).toBeFalse();
    }
  });

  it('warns about missing or invalid frequency even without metadata', () => {
    for (const frequency of [undefined, null, 0, -1, NaN, Infinity, '100']) {
      expect(isFrequencyLow(frequency, undefined)).toBeTrue();
      expect(isFrequencyLow(frequency, [100, 200])).toBeTrue();
    }
  });
});
