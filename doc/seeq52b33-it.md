# SEEQ 52B33 / DQ52B33-250 con T48 — minipro 0.7.4-seeq1

Modifica dedicata alla EEPROM SEEQ parallela **8192 x 8, DIP28, 5 V**.
Il profilo si chiama **52B33@DIP28**; alias: **52B33**, **DQ52B33-250**.
Non selezionare AT28C64B per usare questo driver. Non occorrono Arduino,
adattatori attivi, breadboard o modifiche al firmware del T48.

**Stato: sperimentale, compilato e testato con un modello USB; non collaudato
su T48 e SEEQ reali.** L'eseguibile pronto elimina il lavoro di compilazione,
ma non sostituisce il collaudo elettrico descritto sotto.

## Hardware e posizionamento

Serve un **XGecu T48**, il cavo USB e la SEEQ rimossa dalla cartuccia ELKA.
Nessun cavo ICSP/ISP collegato; nessun collegamento alla macchina.

Il driver usa il frame virtuale T48 a 40 pin: DIP28 sul lato alto dello
zoccolo, con tacca rivolta al lato superiore indicato dal programmatore.
Il pin 1 del chip si trova nella prima posizione in alto a sinistra;
il pin 28 e nella posizione in alto a destra della stessa riga. Restano
vuote le dieci righe inferiori dello ZIF48. Non usare il posizionamento
in basso previsto da alcuni vecchi TL866.

Il frame virtuale e quello del codice bitbang upstream: pin chip 1..14
su frame 1..14, pin chip 15..28 su frame 27..40. **Questi numeri virtuali
non sono la numerazione fisica 1..48 dello zoccolo.** Questa mappatura
va confermata con le misure a zoccolo vuoto prima di inserire il chip.

Collegamenti applicati dal programma:

- SEEQ 28: VCC circa +5 V; SEEQ 14: GND.
- SEEQ 1: /CC alimentato fisso a +5 V; nessuna funzione Chip Clear.
- SEEQ 20/22/27: /CE, /OE, /WE indipendenti.
- SEEQ 26: ingresso del tester con pull-up, nessun driver di potenza.
- Tutti gli altri pin seguono il datasheet SEEQ; nessuna VPP sullo zoccolo
  e nessuna alimentazione esterna ICSP abilitata.

## Windows: usare l'eseguibile incluso

Estrarre tutto lo ZIP. Aprire PowerShell nella cartella che contiene
`minipro.exe` e `infoic.xml`. Chiudere Xgpro mentre si usa minipro.
La build e Windows x64 e non richiede Python, Linux o DLL di terze parti.
Usa il backend WinUSB upstream e richiede che il T48 sia accessibile con
il driver USB appropriato. Il programma non installa o cambia driver USB.

Prima prova, senza memoria nello zoccolo:

```powershell
.\minipro.exe -q T48 --infoic infoic.xml -L 52B33
.\minipro.exe -q T48 --infoic infoic.xml -d '52B33@DIP28'
.\minipro.exe -k
```

I primi due comandi interrogano soltanto il database e funzionano senza
programmatore collegato. Il terzo interroga il programmatore USB.
Se indica `No programmer found`, il collegamento/driver USB resta da
configurare: non e una prova di incompatibilita della memoria. Non cambiare
alla cieca il driver di un altro dispositivo o quello del T48 dell'amico.
Su Linux/macOS si puo compilare lo stesso sorgente con il backend libusb.

## Prima prova elettrica: zoccolo vuoto

Con il programmatore scollegato, predisporre puntali/clip evitando corti.
Non inserire una memoria in questa fase. Collegare il T48 e lanciare:

```powershell
.\minipro.exe --infoic infoic.xml -p '52B33@DIP28' -r vuoto.bin
```

Durante la lettura misurare nelle posizioni previste per la SEEQ:

- pin 28 rispetto a 14: 4,5..5,5 V, nominalmente 5 V;
- pin 1: alto a circa 5 V;
- pin 27: sempre alto, senza impulsi bassi durante la lettura.

Per controllare gli impulsi, sempre senza memoria, preparare un BIN da
8192 byte con valori 55 e acquisire /WE, /OE, /CE e i dati con un analizzatore
logico compatibile 5 V o oscilloscopio. Il comando di scrittura sullo zoccolo
vuoto deve fermarsi alla prima verifica fallita. /WE deve rimanere basso per
almeno 12 ms; /OE e /CC devono essere alti. Gli indirizzi e i dati devono
essere stabili. Un multimetro non verifica le temporizzazioni.

La lettura dell'ADC interno viene controllata dal driver, ma la sua
conversione/calibrazione viene dal codice upstream: non sostituisce la
misura sullo zoccolo. Se non rientra in 4,5..5,5 V, il programma si ferma.

## Lettura della SEEQ: nessuna scrittura

Dopo i controlli, scollegare l'USB, inserire il chip orientato correttamente
e ricollegare. Usare nomi nuovi: minipro puo sovrascrivere i file indicati.

```powershell
.\minipro.exe --infoic infoic.xml -p '52B33@DIP28' -r originale-1.bin
.\minipro.exe --infoic infoic.xml -p '52B33@DIP28' -r originale-2.bin
Get-Item originale-1.bin, originale-2.bin | Select-Object Name, Length
Get-FileHash originale-1.bin, originale-2.bin -Algorithm SHA256
```

Entrambi i file devono avere **8192 byte** e lo stesso SHA256. Conservare
entrambi i dump prima di modificare il chip. Tutti FF o tutti 00 non provano
che la memoria sia presente, leggibile o integra. Anche due letture uguali
non provano il contenuto ELKA corretto o tutte le linee d'indirizzo.

## Scrittura e verifica

Serve un file valido da 8192 byte, adatto alla cartuccia. Il progetto non
contiene un dump ELKA e non ricostruisce dati corrotti.

```powershell
.\minipro.exe --infoic infoic.xml -p '52B33@DIP28' -w corretto.bin
.\minipro.exe --infoic infoic.xml -p '52B33@DIP28' -m corretto.bin
```

Il driver legge ogni byte, salta quelli gia corretti, cancella con FF
quelli che lo richiedono, verifica la cancellazione, scrive e verifica.
La normale scrittura non esegue una cancellazione globale preliminare.
La verifica del singolo byte rimane obbligatoria anche se si usa `-v`;
normalmente minipro esegue anche la verifica globale finale.

La cancellazione completa, se davvero desiderata, usa soltanto cicli per byte:

```powershell
.\minipro.exe --infoic infoic.xml -p '52B33@DIP28' -E
```

`-E` modifica il chip: acquisire prima i due backup. Non viene mai usato
Chip Clear. Non combinare opzioni di altre EEPROM: il driver rifiuta
ICSP, `-z`, `-B`, protezioni software e profili/timing diversi da quelli
previsti. `-s/-S` non consentono immagini di scrittura di dimensione diversa.

Le operazioni possono richiedere diversi minuti per i trasferimenti USB
individuali. Ctrl-C richiede l'arresto dopo aver completato l'impulso in
corso; il file sul chip puo risultare parziale. Un crash del PC, la
terminazione forzata o un guasto USB non possono garantire la disattivazione
dei segnali: scollegare l'USB e conservare il BIN originale. Non estrarre
o inserire il chip con il T48 alimentato.

## Verifiche eseguite

- Compilazione/link Windows x64 riusciti: w64devkit 2.10.1, GCC 16.2.0,
  zlib 1.3.2 statica, backend WinUSB nativo.
- Nuovo driver e test compilati con `-Wall -Wextra -Werror`.
- **11 test del protocollo superati**, con il vero codice del driver e
  del backend bitbang T48; USB e tempo sono simulati. Controllano i pacchetti
  di potenza (VPP ed ICSP disabilitati), indirizzi e dati, assenza di conflitto
  bus, /CC alto e impulsi >=12 ms, lettura completa, immagine completa,
  erase-before-write, byte invariati, limiti di memoria, byte guasti,
  sovracorrente, errore USB e segnale di interruzione durante l'impulso.
- Query reale dell'eseguibile al database: profilo e alias caricati,
  DIP28, 8192 byte, algoritmo personalizzato 0x02, indicazione T48-only.
- `git diff --check` senza errori.

Non eseguiti: collegamento a un T48 reale, misure elettriche, lettura di
una SEEQ reale, scrittura e prove sulla cartuccia ELKA. L'effettivo comportamento
del firmware proprietario del T48 resta da verificare. Non e stato aggiunto
supporto per altri SEEQ, varianti H, RAM o DRAM.

## Sorgenti, patch e riproduzione

Base: [whaslbeck/minipro](https://github.com/whaslbeck/minipro), commit
`cfe2171ca1b23399558b8d2a603a2ac3fa3faf5d`. Le modifiche sono nel driver
`src/seeq52b33.c`, dispatch bitbang, profilo XML, gestione alimentazione T48,
validazione CLI e due helper stringa per compilare con MinGW-w64.
La distribuzione include i sorgenti completi e una patch rispetto alla base.

Linux/macOS, dalla cartella dei sorgenti, con le dipendenze del README:

```
make
make test-seeq
./minipro -q T48 --infoic infoic.xml -d 52B33@DIP28
```

Windows, per ricompilare, installare/estrarre w64devkit e compilare zlib
statica con `make -f win32/Makefile.gcc libz.a`, poi:

```
py tools/build_windows_seeq.py --gcc C:/percorso/w64devkit/bin/gcc.exe --zlib C:/percorso/zlib-1.3.2
py tools/build_windows_seeq.py --gcc C:/percorso/w64devkit/bin/gcc.exe --test
```

La directory `build-seeq` contiene il programma; `build-seeq-tests` i test.

Riferimento elettrico: [SEEQ Data Book 1988](https://archive.decromancer.ca/bitsavers.org/components/seeq/_dataBooks/1988_SEEQ_Data_Book.pdf),
52B33/52B33H, ottobre 1987, pagine stampate 1-11..1-16.
Licenza minipro e modifiche: GPL-3.0-or-later, vedi LICENSE.
