# env.ps1 - activate the ESP-IDF 6.0.2 environment for this machine.
#
# Usage (note the leading dot, which keeps the environment in THIS shell):
#   . D:\esp32-demo\env.ps1
#
# IDF_PYTHON_ENV_PATH must be pinned because export.ps1 derives the venv name
# from the Python version it expects (idf6.0_py3.11_env), while this machine's
# environment was installed as idf6.0_py3.13_env.
$env:IDF_PYTHON_ENV_PATH = "C:\Users\user1\.espressif\python_env\idf6.0_py3.13_env"
. "D:\espidf\.espressif\v6.0.2\esp-idf\export.ps1"
