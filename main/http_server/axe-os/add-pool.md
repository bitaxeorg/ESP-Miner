# Adding a new mining pool implementation
This guide explains how to add support for a new mining pool to the frontend using the `MiningPool` interface.
Each pool provides logic for handling pool-specific URLs, rejected share explanations, and quick links to the pool’s web UI.

## Adding a new service
Navigate to the pools directory and use following command to add a new service file for your pool:
```bash
ng generate service <poolname>
```

## Implement the service
Make your new service implements the `MiningPool` interface or extend any existing mining pool implementation (e.g. `PublicPoolService`) in case your new pool is a derivative of an existing one. Implement or override all requiered methods to fit your pool.

If you pool does not provide any share reject reasons, you may extends the generic-pool serivce.