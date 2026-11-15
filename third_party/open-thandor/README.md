# Test15 simulation engine

This directory contains the Open Thandor gameplay sources used by Simulation Lab.
The engine is based on Open Thandor commit
`57d004102a2e5bb0f070dfec55ba9a7e714e8b63` with Jandrenator's Test15 AI applied.
The editor build compiles these sources directly. The editor adapter supplies
the platform and rendering snapshot interfaces, and Test15 reads
`advanced_ai_jandrenator.ini` for its run settings.

See [LICENSE](LICENSE) for the Open Thandor license.
