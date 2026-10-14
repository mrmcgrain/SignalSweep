"""Bundle the complete generic firmware default signature table.

No packet/Remote-ID/tracker decoder parity is implied. Expand dynamic defaults
and named category constants; fail instead of silently omitting unknown rules.
"""
import json
from pathlib import Path
import re

app = Path(__file__).resolve().parents[1]
source = (app.parent / 'firmware/src/mode_watchers_watch.cpp').read_text()
rules = []
arrays = {name: re.findall(r'"([^"\\]*)"', body) for name, body in
          re.findall(r'const char\*\s+(\w+)\[\]\s*=\s*\{(.*?)\};', source, re.S)}
for body in re.findall(r'addRule\((.*?)\);', source, re.S):
    tokens = re.findall(r'"(?:\\.|[^"\\])*"|[A-Za-z_][A-Za-z_0-9]*|\d+', body)
    if len(tokens) not in (6, 7, 8):
        raise ValueError('Unrecognized default rule: '+body)
    expansion = [None]
    if tokens[2] == 'oui': expansion = arrays['flockOuis']
    elif tokens[4] == 'name': expansion = arrays['bleNames']
    for expanded in expansion:
        values = []
        for token in tokens[:6]:
            if token.startswith('"'): values.append(json.loads(token))
            elif token == 'HACKING_GEAR_CATEGORY': values.append('Hacking gear')
            elif token in ('oui', 'name') and expanded is not None: values.append(expanded)
            else: raise ValueError('Unrecognized rule token '+token)
        rule = dict(zip(['name', 'category', 'oui', 'mfg_id', 'device_name', 'service_uuid'], values))
        if len(tokens) >= 7:
            if not tokens[6].isdigit(): raise ValueError('Unrecognized rule weight')
            rule['weight'] = int(tokens[6])
        if len(tokens) == 8: rule['ssid_prefix'] = json.loads(tokens[7])
        rules.append(rule)
(app / 'public/native-rules.json').write_text(json.dumps(rules, indent=2)+'\n')
print(f'Bundled complete {len(rules)} generic rules; specialized packet decoders are a separate parity requirement')
