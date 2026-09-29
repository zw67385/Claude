# Tools

Source assets are not in git (4.5 GB). To rebuild the data:

    F2F=/path/to/FearsToFathomAssets/assets2/Assets/Localization/Tables python3 tools/extract_text.py
    YARN=/path/to/Assets/MonoBehaviour/Project.asset python3 tools/yarn_dis.py
    python3 tools/yarn_dump.py

`unity.py` is a small reader for the flattened Unity YAML scenes (needs PyYAML with libyaml).
