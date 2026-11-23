# StadiaPad

Один `.exe`, который превращает геймпад Google Stadia (USB или Bluetooth) в виртуальный Xbox 360 геймпад.

## Установка на новый компьютер
1. Скопировать `bin\StadiaPad.exe` куда угодно (например, `C:\SOFT\StadiaPad\`).
2. Запустить. Если драйверов нет — нажать «Да», подтвердить права администратора, перезагрузиться.
3. После перезагрузки запустить StadiaPad снова (ярлыком). Программа живёт в трее; автозапуска нет — запускается только вручную.

## Что делает программа
- Встроены официальные подписанные установщики **ViGEmBus 1.22.0** и **HidHide 1.5.230** (Nefarius Software Solutions); ставятся тихо при первом запуске.
- Сама настраивает HidHide: добавляет себя в белый список, скрывает Stadia (USB и Bluetooth) от игр — без двойного ввода.
- При выходе возвращает геймпад системе (снимает скрытие), так что без программы он виден как обычное устройство.
- Меню в трее (правая кнопка): статус геймпада и драйверов, «Скрывать оригинальный геймпад», «Переподключить», «Установить драйверы…» / «Удалить драйверы…», «Выход».
- Настройки: `HKCU\Software\StadiaPad`. Журнал: `%LOCALAPPDATA%\StadiaPad\StadiaPad.log`.

## Сборка
Нужны Visual Studio Build Tools 2022 (C++):
```
powershell -ExecutionPolicy Bypass -File build.ps1
```
Результат: `bin\StadiaPad.exe` (x64, статический CRT — на целевом компьютере ничего доустанавливать не нужно).

## Структура
- `src/main.c` — логика приложения, меню, маппинг Stadia → Xbox 360
- `src/stadia.c`, `src/hid.c` — чтение геймпада (из stadia-vigem, с исправлениями)
- `src/hidhide.c` — прямое управление драйвером HidHide через `\\.\HidHide`
- `src/drivers.c` — проверка/установка/удаление драйверов
- `src/settings.c` — настройки
- `src/log.c` — журнал работы
- `drivers/` — официальные установщики, вшиваются в exe
- `third_party/ViGEmClient` — клиентская библиотека ViGEm

## Лицензии и благодарности
- stadia-vigem (walkco), Mi-ViGEm (grayver) — MIT, см. `licenses/`
- ViGEmClient, ViGEmBus, HidHide — Nefarius Software Solutions e.U., см. `licenses/`
- Иконка — Freepik / flaticon.com
