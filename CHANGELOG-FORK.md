# Changelog - Voltage Monitoring Fork

All notable changes to the voltage monitoring feature will be documented in this file.

## [1.1.0-voltage-monitoring] - 2024-12-28

### Added
- VoltageMonitorComponent for AxeOS UI
- Real-time ASIC voltage display with color coding
- Chain-level voltage statistics
- Integration with system hash rate and frequency data
- Sidebar navigation menu item
- Auto-refresh functionality (5-second interval)
- Hardware detection for ADS1115

### Changed
- Updated app routing to include /monitor route
- Modified app.menu.component.ts to add ASIC Monitor link
- Enhanced UI to show voltage, frequency, and hash rate per ASIC

### Technical
- Created voltage.service.ts for API integration
- Implemented responsive grid layout for ASIC display
- Added TypeScript interfaces for voltage data structures
- Integrated with existing PrimeNG theme

## [1.0.0-voltage-monitoring] - 2024-12-15

### Added
- Initial voltage monitoring firmware implementation
- Basic API endpoint for voltage data
