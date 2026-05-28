Přesný postup:

Commitněte a pushněte změny, počkejte na GitHub Actions build a stáhněte firmware zip.
V počítači odeberte staré Bluetooth párování původní Sofle.
Flashněte settings_reset.uf2 do donglu: dvojklik na reset, objeví se disk NICENANO, nakopírujte settings_reset.uf2.
Stejný settings_reset.uf2 flashněte i do levé půlky.
Stejný settings_reset.uf2 flashněte i do pravé půlky.
Pak flashněte finální firmware: sofle_dongle.uf2 do donglu, sofle_left.uf2 do levé půlky a sofle_right.uf2 do pravé
půlky.
Po finálním flashi všechna tři zařízení odpojte a znovu zapněte nebo resetněte během pár sekund po sobě, aby se půlky
znovu spárovaly s donglem.
K počítači pak používejte jen dongle. Pokud ho budete mít v USB, žádné Bluetooth párování s počítačem není potřeba.
Pokud ho chcete používat přes Bluetooth, párujte pouze dongle, nikdy ne levou ani pravou půlku.
Důležitý detail: během settings reset firmware se nic nebude normálně hlásit přes Bluetooth, to je očekávané. Lokálně
jsem ověřil syntaxi změny v build souboru, ale samotný GitHub Actions build jsem tady nespouštěl.

Pokud chcete, můžu vám ještě připravit i krátký checklist, podle kterého poznáte, že se dongle a obě půlky správně znovu
spárovaly.
Pokud chcete, můžu vám rovnou sepsat i přesný git commit a push sled příkazů pro tuto změnu.

## Checklist po flashi

- [ ] V GitHub Actions buildu jsou ke stažení aspoň čtyři UF2 soubory: `settings_reset`, `sofle_dongle`, `sofle_left`, `sofle_right`.
- [ ] `settings_reset.uf2` byl nahrán do všech tří nice!nano: dongle, levá půlka, pravá půlka.
- [ ] Po `settings_reset` jste do všech tří desek nahráli finální firmware se správným cílem.
- [ ] Po finálním flashi jste dongle i obě půlky resetovali nebo zapnuli znovu v krátkém sledu.
- [ ] Po připojení donglu přes USB se klávesnice na počítači chová jako USB HID zařízení.
- [ ] Na počítači nepárujete levou ani pravou půlku; s hostem komunikuje jen dongle.
- [ ] Pokud dongle používáte přes Bluetooth, v seznamu zařízení párujete pouze dongle.
- [ ] Stisky z levé i pravé poloviny se přenášejí do počítače přes dongle.

## Když něco nesedí

- Pokud funguje jen jedna půlka, resetujte ji a dongle znovu během pár sekund po sobě.
- Pokud se host snaží párovat s levou nebo pravou půlkou, nebyl dokončen reset nebo je nahraný špatný firmware.
- Pokud po USB připojení nereaguje nic, zkontrolujte, že v donglu je opravdu `sofle_dongle.uf2`, ne firmware pro levou nebo pravou půlku.
- Pokud se po změně topologie chová Bluetooth divně, odeberte staré párování v hostu a celý `settings_reset` postup zopakujte.