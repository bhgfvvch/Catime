const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const tray = fs.readFileSync(path.join(root, 'src', 'tray', 'tray_menu.c'), 'utf8');
if (!tray.includes('GetLocalizedString(L"统计", L"Statistics")')) {
  throw new Error('Tray Statistics command must use the shared Statistics key');
}
if (tray.includes('GetLocalizedString(L"统计...", L"Statistics...")')) {
  throw new Error('Tray still uses the missing Statistics... translation key');
}
console.log('statistics_i18n_tests: PASS');
