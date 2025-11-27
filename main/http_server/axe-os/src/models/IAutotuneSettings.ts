export interface IAutotuneSettings {
  power_limit: number;
  fan_limit: number;
  max_volt_asic: number;
  max_freq_asic: number;
  max_temp_asic: number;
  max_temp_vr: number;
  auto_tune: boolean;
  osh_pow_limit: number;
  osh_fan_limit: number;
}