<div align="center">

<a href="README.md"><img src="https://img.shields.io/badge/-English-30363d?style=for-the-badge" alt="English" height="38"></a>&nbsp;&nbsp;<a href="README.it.md"><img src="https://img.shields.io/badge/-Italiano-0a66c2?style=for-the-badge" alt="Italiano" height="38"></a>

[![status](https://img.shields.io/badge/stato-in%20test-orange?style=flat-square)](#)
[![release](https://img.shields.io/github/v/release/N1k0droid/t-deck-sonde-tracker?include_prereleases&sort=semver&style=flat-square)](https://github.com/N1k0droid/t-deck-sonde-tracker/releases)
[![license](https://img.shields.io/badge/license-GPL--3.0-blue?style=flat-square)](LICENSE)
[![hardware](https://img.shields.io/badge/LilyGO-T--Deck%20Plus%20433%20MHz-2ea44f?style=flat-square)](https://github.com/Xinyuan-LilyGO/T-Deck)
[![radio](https://img.shields.io/badge/ESP32--S3-SX1262-555?style=flat-square)](#)
[![PlatformIO](https://img.shields.io/badge/built%20with-PlatformIO-f5822a?style=flat-square)](https://platformio.org)

<br>

<img src="docs/img/tdeck.png" alt="T-Deck SondeTracker su un LilyGO T-Deck Plus" width="300">

### Caccia alle radiosonde (Vaisala RS41, Graw DFM, Meteomodem M10/M20, InterMet iMet-54) con il solo LilyGO T-Deck Plus (433 MHz)

<table>
  <tr>
    <td align="center"><img src="docs/img/hunt.png" width="260"><br><sub>Caccia: distanza e bussola</sub></td>
    <td align="center"><img src="docs/img/map.png" width="260"><br><sub>Mappa offline e previsione di caduta</sub></td>
    <td align="center"><img src="docs/img/signal.png" width="260"><br><sub>Avvicinamento: signal meter</sub></td>
  </tr>
</table>

</div>

> [!WARNING]
> **Firmware in fase di test.** Funziona, ma alcune parti non sono ancora state provate sul campo: vedi [Funzioni in sviluppo e da verificare](#funzioni-in-sviluppo-e-da-verificare).

## Indice
- [Cos'è](#cosè)
- [Stato del progetto](#stato-del-progetto)
- [Cosa serve](#cosa-serve)
- [Installazione](#installazione)
- [Primo avvio](#primo-avvio)
- [Wi-Fi e pagina web, passo per passo](#wi-fi-e-pagina-web-passo-per-passo)
- [Comandi](#comandi)
- [Le pagine](#le-pagine)
- [Impostazioni](#impostazioni)
- [Mappa offline](#mappa-offline)
- [App di caccia sul telefono](#app-di-caccia-sul-telefono)
- [Internet tramite l'hotspot del telefono](#internet-tramite-lhotspot-del-telefono)
- [Interfaccia web](#interfaccia-web)
- [Server di debug](#server-di-debug)
- [Frame completi e sensore d'ozono](#frame-completi-e-sensore-dozono)
- [Tipi di sonda e preset](#tipi-di-sonda-e-preset)
- [Invio dei dati: SondeHub, radiosondy.info, aprs.fi](#invio-dei-dati-sondehub-radiosondyinfo-aprsfi)
- [SondeHub: serve aiuto](#sondehub-serve-aiuto)
- [File sulla scheda SD](#file-sulla-scheda-sd)
- [Come è stato provato](#come-è-stato-provato)
- [Funzioni in sviluppo e da verificare](#funzioni-in-sviluppo-e-da-verificare)
- [Limiti noti](#limiti-noti)
- [Glossario](#glossario)
- [Compilare dai sorgenti](#compilare-dai-sorgenti)
- [Crediti e licenza](#crediti-e-licenza)

## Cos'è
Le radiosonde sono piccoli strumenti meteo appesi a un pallone: salgono fino a 30-35 km, il pallone scoppia e ricadono con un paracadute. Trasmettono la loro posizione nella banda 400-406 MHz e molti radioamatori le vanno a cercare e recuperare: è la "caccia alla sonda".

Di solito servono un ricevitore (per esempio una scheda TTGO), un telefono con un'app e una mappa. **T-Deck SondeTracker fa tutto con il solo T-Deck Plus**, anche senza telefono e senza internet:

- riceve e decodifica le sonde **Vaisala RS41** con la radio interna del T-Deck, **anche a frame interi** (meglio col segnale debole), le **Graw DFM-06/09/17**, le **Meteomodem M10/M20** e le **InterMet iMet-54**;
- legge il **sensore d'ozono** (OIF411) delle sonde per l'ozono;
- mostra **distanza, direzione, quota e velocità** della sonda, con una bussola che indica dove andare;
- calcola **dove atterrerà** la sonda, dal vento misurato dalla sonda stessa;
- **mappa offline** dalla scheda SD, con la tua posizione, la sonda, il suo percorso e il punto di caduta;
- **pagina di avvicinamento** con un signal meter sonoro per gli ultimi metri;
- **scansione continua** (prima le frequenze dei preset, poi tutta la banda) per trovare una sonda di cui non conosci la frequenza;
- salva il **percorso di ogni sonda** sulla SD (CSV ed esportazione KML per Google Earth);
- quando vuoi, fa da **ricevitore per le app di caccia** (MySondyGO, Hunter Tracker, TrovaLaSonda) e **legge SondeHub** tramite l'hotspot del telefono;
- se lo accendi, **invia le sonde** a radiosondy.info e **la posizione della tua stazione** a radiosondy.info e aprs.fi; tiene ogni frame nel formato di SondeHub sulla SD, per far approvare il firmware da SondeHub ([serve aiuto](docs/SONDEHUB.md)).

<p align="center">
  <img src="docs/img/home.png" width="260"> <img src="docs/img/gps.png" width="260"> <img src="docs/img/nearby.png" width="260">
</p>

## Stato del progetto
Il firmware è in **fase di test**. La ricezione delle RS41 (anche su voli reali), la schermata di caccia, la mappa, i temi, la lettura di SondeHub e gli invii a radiosondy.info e aprs.fi funzionano e sono stati provati sul T-Deck Plus; la scansione e gli altri tipi di sonda al banco. Come sono state fatte le prove: [Come è stato provato](#come-è-stato-provato). Altre funzioni sono complete ma ancora da provare sul campo: l'elenco è in [Funzioni in sviluppo e da verificare](#funzioni-in-sviluppo-e-da-verificare).

Le schermate di questa pagina riproducono quelle del T-Deck con dati di esempio (una caccia simulata in Sicilia).

Segnalazioni, prove e suggerimenti sono benvenuti nella sezione **Issues** di GitHub.

## Cosa serve
- **LilyGO T-Deck Plus, versione a 433 MHz** (radio LoRa SX1262 e GPS integrato). Il T-Deck Plus si vende nelle versioni a 433, 868 e 915 MHz: solo quella a **433 MHz** ha il circuito radio accordato vicino alla banda delle sonde (400-406 MHz). Le versioni a 868/915 MHz ricevono le sonde molto peggio, o per niente.
- Il T-Deck senza "Plus" non ha il GPS: il firmware funziona, ma senza la tua posizione.
- Una **scheda microSD** (FAT32), consigliata: serve per i registri, i percorsi e la mappa.
- Un'**antenna** adatta ai 400-406 MHz: quella a 433 MHz fornita con il T-Deck va bene per iniziare.
- Facoltativo: un telefono con hotspot, per internet e per le app di caccia.

## Installazione
Il file da installare si scarica dalla pagina **[Releases](https://github.com/N1k0droid/t-deck-sonde-tracker/releases)**: è `t-deck-sonde-tracker-v-<versione>-beta<n>.bin`, per esempio `t-deck-sonde-tracker-v-0.95-beta4.bin` (le versioni 0.x sono beta; dalla 1.0.0 `t-deck-sonde-tracker-v-<versione>.bin`). È un'immagine completa (bootloader, partizioni e programma), da scrivere all'indirizzo 0.

### Metodo 1: con il firmware Launcher (consigliato)
Il [Launcher](https://github.com/bmorcelli/Launcher) è un firmware open source per dispositivi ESP32, T-Deck compreso, che fa da gestore di avvio: resta sul dispositivo e permette di installare e alternare i firmware tenuti sulla scheda SD, senza PC. La caccia alle radiosonde non è un'attività quotidiana: con il Launcher il T-Deck può usare un altro firmware tutti i giorni e passare a T-Deck SondeTracker quando c'è un lancio da seguire.

Se sul T-Deck il Launcher non c'è ancora, lo si installa una volta sola dalla pagina [Launcher Flasher](https://bmorcelli.github.io/Launcher/) (Chrome o Edge), seguendo le istruzioni indicate lì.

1. Copia il file `.bin` nella cartella `downloads` della scheda SD (creala se manca), dove il Launcher cerca i firmware. Con T-Deck SondeTracker già installato, le versioni successive si possono copiare anche dal browser, senza togliere la scheda: vedi [Interfaccia web](#interfaccia-web), pagina **Files**.
2. Inserisci la SD nel T-Deck e avvia il Launcher.
3. Scegli **SD**, poi il file `t-deck-sonde-tracker-v-….bin`, e conferma l'installazione.
4. Al termine il T-Deck si riavvia con T-Deck SondeTracker.

### Metodo 2: dal browser, via cavo USB
[ESP Web Tool](https://esptool.spacehuhn.com) è una pagina web open source che scrive il firmware sulle schede ESP32: non c'è niente da installare. Il browser comunica con il T-Deck via USB tramite l'API Web Serial, che supportano solo Chrome ed Edge da computer.

Questo metodo scrive tutta la memoria: **sostituisce il Launcher**, se lo hai.

1. Collega il T-Deck al PC con un cavo USB-C **che trasporti dati** (alcuni cavi caricano soltanto).
2. Apri con Chrome o Edge la pagina [esptool.spacehuhn.com](https://esptool.spacehuhn.com).
3. Premi **Connect** e scegli la porta del T-Deck. Se non compare: tieni premuto il trackball (pulsante BOOT), premi e rilascia il tasto di reset sul lato, poi rilascia il trackball, e riprova.
4. Scegli il file `.bin`, verifica che l'offset sia `0x0` e premi **Program**.
5. Al termine premi il tasto di reset.

### Metodo 3: da riga di comando (esptool)
```bash
pip install esptool
esptool --chip esp32s3 write-flash 0x0 t-deck-sonde-tracker-v-0.95-beta4.bin
```
Con le versioni vecchie di esptool il comando è `esptool.py ... write_flash ...`.

## Primo avvio
1. All'accensione compare per un attimo il logo, poi la **Home**.
2. Esci all'aperto: il GPS ha bisogno di vedere il cielo. Il primo aggancio può richiedere qualche minuto; nella pagina **GPS** vedi i satelliti che arrivano.
3. Imposta la frequenza della sonda: dalla pagina **Hunt** premi `f`, scrivi la frequenza in MHz (per esempio `404.5`) e premi Invio. Se non la conosci, usa **Scan** dalla Home.
4. Quando la sonda viene ricevuta, senti un breve "tic" a ogni pacchetto e la pagina Hunt si riempie di dati.

> La frequenza delle sonde lanciate vicino a te si trova su [SondeHub](https://tracker.sondehub.org) o dal T-Deck stesso, nella pagina **Nearby** (serve internet).

## Wi-Fi e pagina web, passo per passo
Il Wi-Fi serve per internet (sonde vicine, ora esatta, invii) e per usare il T-Deck dal browser. Il T-Deck vede solo le reti a **2,4 GHz**, non quelle a 5 GHz. La rete più comoda è l'hotspot del telefono.

1. Sul telefono accendi l'hotspot. Su iPhone attiva anche **Massimizza compatibilità**: così l'hotspot usa i 2,4 GHz.
2. Sul T-Deck vai in **Settings**, scheda **Network**, e premi **Scan networks**. Dopo qualche secondo compare l'elenco delle reti.
3. Scegli la tua rete. Se è protetta si apre un riquadro: scrivi la password (la vedi mentre scrivi) e premi Invio.
4. Se vuoi ritrovarla la prossima volta: **Saved networks**, poi la prima riga **+ save this network**.
5. Per internet accendi **Internet via Wi-Fi** (stessa scheda). Per la pagina web vai nella scheda **System** e accendi **Web interface**.
6. Dopo qualche secondo la voce **Web interface** mostra l'indirizzo del T-Deck: quattro numeri separati da punti.
7. Sul telefono o sul PC, collegati alla stessa rete, apri il browser e scrivi `http://` seguito da quell'indirizzo (`http://<ip>/`).
8. Il browser chiede utente e password: utente `tdeck`, password `radiosonde`. La password si cambia con la voce **Web password**.

Cosa fa la pagina: [Interfaccia web](#interfaccia-web).

## Comandi
Il T-Deck si usa con **tastiera**, **trackball** e **schermo touch**: ogni funzione è raggiungibile in tutti e tre i modi.

- **Home:** tocca un pulsante, oppure muovi il trackball e premilo, oppure premi il numero del pulsante.
- **In basso in ogni pagina:** ◀ **Indietro** a sinistra e ⌂ **Home** a destra. Da tastiera: Indietro = Backspace (o Esc); Home = `h`.
- **`v`** passa velocemente tra le tre pagine di caccia: Hunt, Map e Signal; **`m`** apre la mappa.
- **Trackball:** nelle liste sposta la selezione; sulla mappa sposta la vista (un clic cambia il centro, come `c`); in Hunt, Signal e Info su/giù cambia la frequenza di ±1 kHz; **tenuta premuta 3 secondi** blocca o sblocca lo schermo.
- **`Alt`+`D`** cancella la voce selezionata negli elenchi dei preset e delle reti salvate (dopo una conferma).
- **Domande Sì/No:** `y` risponde sì, `n` o Indietro risponde no. Invio sceglie il pulsante evidenziato, che all'inizio è No.
- **Frequenza in alto a sinistra:** un tocco apre la scrittura di una nuova frequenza, come `f` (non nella pagina Map).
- **Mentre scrivi un testo:** Esc, oppure Backspace con il campo vuoto, oppure un tocco sullo schermo annulla.

### Tasti
| Tasto | Funzione |
|---|---|
| `h` / Backspace | Home / indietro |
| `m` | mappa |
| `v` | cambia pagina di caccia: Hunt, Map, Signal |
| `f` | scrivi una nuova frequenza (cifre, poi Invio) |
| `w` o `+` / `q` o `-` | frequenza +1 / −1 kHz |
| `e` / `d` | frequenza +10 / −10 kHz |
| `p` | preset successivo dello stesso paese |
| `a` | salva frequenza e tipo di sonda attuali come nuovo preset: si scrive il nome, poi si conferma con `y` |
| `j` | cerca nei preset: scrivi un nome, ad esempio `trapani`, poi Invio |
| `y` | tipo di sonda: auto / RS41 / DFM / M10/M20 / iMet54 (nelle domande vuol dire sì) |
| `x` | avvia la scansione (di nuovo `x` per fermarla) |
| `n` | la bussola punta alla sonda oppure al punto di caduta previsto |
| `t` | bersaglio manuale: scrivi latitudine e longitudine, per esempio `38.0176 12.5365` |
| `s` | toglie tutti i suoni; premuto di nuovo li rimette ai volumi di prima |
| `k` | esporta il percorso della sonda in KML (per Google Earth) |
| `i` / `o` | mappa: zoom avanti / indietro |
| `c` | mappa: centra su entrambi, su di te, sulla sonda |
| `g` | mappa: codice QR con la posizione della sonda, da inquadrare col telefono |
| `u` | polarità del segnale: automatica / normale / invertita (normalmente lasciala automatica) |
| `r` | azzera i contatori, la posizione ricevuta e la previsione del T-Deck; un bersaglio scritto con `t` resta |
| `l` | registro dei dati grezzi acceso/spento |
| `Alt`+`B` | accende/spegne la luce della tastiera |
| trackball tenuta premuta 3 s | blocca / sblocca tasti, trackball e touch (un lucchetto nella barra in alto); radio, allarmi e invii continuano. Sempre sbloccato dopo il riavvio |

## Le pagine

### Home
Otto pulsanti: **Hunt**, **Map**, **Signal**, **GPS**, **Nearby**, **Info**, **Settings**, **Scan**. Sotto ogni nome una riga breve dice come sta quella pagina: per esempio la distanza dalla sonda, lo zoom della mappa, il segnale in dBm, il fix del GPS, quante sonde ha trovato SondeHub.

### Hunt (caccia)
<img src="docs/img/hunt.png" width="320" align="right">

La pagina principale durante la caccia:
- in alto la barra di stato: spia dei pacchetti, frequenza, seriale della sonda, `WEB` con l'interfaccia web accesa (`DBG` con le pagine di debug accese; un lucchetto quando lo schermo è bloccato), `SH` / `RS` / `FI` in rosso quando un invio è acceso (SondeHub, radiosondy.info, aprs.fi; se ne sono accesi più di uno, piccoli uno sopra l'altro), `BT` per le app, `SD`, satelliti del tuo GPS, batteria;
- la **distanza** dalla sonda in grande e la **bussola**;
- quota, velocità verticale e orizzontale della sonda;
- in basso messaggi, punto di caduta previsto e suggerimenti.

**Come leggere la bussola.** Il T-Deck non ha una bussola magnetica: la direzione la ricava dal GPS mentre ti muovi.
- **AHEAD** (verde): stai camminando o guidando. Il triangolo in alto è la tua direzione, la freccia è la sonda, le lettere N E S W ruotano e mostrano i veri punti cardinali. Segui la freccia.
- **N UP** (gialla): sei fermo. La freccia è riferita al nord: tieni il lato alto dello schermo rivolto verso nord.

In discesa la riga in basso mostra il punto di caduta previsto e il tempo all'atterraggio (`LAND`, oppure `SH LAND` quando è la previsione di SondeHub), alternati alla quota sotto cui il segnale si perderà dietro l'orizzonte ("signal lost below ~… m").
<br clear="right">

### Map (mappa)
<img src="docs/img/map.png" width="320" align="right">

Mostra te (blu), la sonda (rossa), il suo percorso, la linea tra voi due e il punto di caduta previsto: la croce magenta è la previsione del T-Deck, quella azzurra, con il suo percorso tratteggiato, quella di SondeHub (se c'è internet).
- Zoom con `i`/`o` o toccando `+` e `−`.
- `c` centra la vista; il trackball la sposta.
- Il tasto **QR** in alto a sinistra (o `g`) mostra un codice QR: inquadralo con il telefono e si apre la navigazione verso la sonda o verso il punto di caduta.

Le tessere della mappa vanno preparate e copiate sulla SD: vedi [Mappa offline](#mappa-offline). Senza la cartella `/maps` la pagina mostra solo come prepararla. Dove manca una singola tessera il fondo è grigio.
<br clear="right">

### Signal (avvicinamento)
Per gli ultimi metri, quando il GPS non basta: una barra mostra il segnale in tempo reale, con il picco degli ultimi secondi, e il **signal meter** sonoro diventa più acuto e veloce man mano che il segnale cresce. Girando su te stesso, il segnale è più forte nella direzione della sonda.

### GPS
Stato del GPS: fix 2D/3D o ricerca in corso, satelliti con la forza del segnale per ognuno, HDOP e PDOP (più sono bassi, più la posizione è precisa), tempo del primo fix, posizione, quota, velocità, ora locale (o UTC) e stato dell'aiuto al GPS. Il T-Deck aiuta il GPS ad agganciarsi prima comunicandogli ora e posizione approssimata (da internet o dalla sonda).

### Nearby (sonde vicine, serve internet)
<img src="docs/img/nearby.png" width="320" align="right">

Quattro schede, da scegliere toccandole o con i tasti 1-4:
- **Near:** sonde in volo nelle ultime 3 ore entro 300 km. In arancione quelle in discesa, in verde quella che segui. Un tocco la segue: diventa il bersaglio e la radio si sintonizza da sola. Un tocco su quella seguita smette di seguirla.
- **Landed:** voli atterrati entro 400 km, nelle ultime 3 ore, 12 ore, 24 ore o 7 giorni, con il punto di atterraggio. I dettagli di un volo permettono di rendere quel punto il tuo bersaglio.
- **Launches:** lanci programmati dalle stazioni vicine entro la voce **Launch predictions** (24 ore di default), con l'atterraggio previsto; si calcolano quando apri la scheda.
- **Search:** cerca una sonda per seriale (per esempio `X1422184`).
<br clear="right">

### Scan (scansione)
Cerca finché trova una sonda o la fermi (`x`, Indietro o un tocco): prima le frequenze dei preset, poi tutta la banda 400-406 MHz, giro dopo giro, ascoltando nel frattempo i canali più forti. Il grafico tiene il livello più alto visto (in tenue) sotto la spazzata in corso, con la soglia del rumore; un canale già provato senza sonda viene lasciato stare per 10 minuti (segno grigio). Quando decodifica una sonda il T-Deck suona e chiede se tenerla e passare alla pagina Hunt; con No la ricerca continua. Fermandola si torna alla frequenza di prima.

### Info
Dati tecnici di ricezione e della sonda: qualità della decodifica, correzioni, frame ricevuti interi, batteria e temperatura della sonda, kill timer, statistiche, valori del sensore d'ozono.

### Settings (impostazioni)
Vedi la sezione seguente.

## Impostazioni
<img src="docs/img/settings.png" width="320" align="right">

Quattro schede: **Radio**, **Network**, **Display**, **System**. Si passa da una scheda all'altra col trackball a sinistra/destra, con la barra spaziatrice o toccando il nome della scheda. In ogni scheda si sceglie una voce con trackball o touch (oppure, nelle schede fino a dieci voci, con le cifre 1-9 e 0) e la si cambia premendola. I campi di testo si confermano con Invio:
- Invio senza scrivere niente tiene il valore che c'era;
- un solo `-` lo cancella;
- password, passcode e token si vedono mentre li scrivi.

Nella scheda Network le voci sono più di dieci, quindi le cifre non funzionano: usa trackball o touch. Nella colonna dei valori "set" vuol dire "non ancora impostata", "change" che c'è già.
<br clear="right">

| Scheda | N. | Voce | Predefinito | A cosa serve |
|---|---|---|---|---|
| Radio | 1 | Sonde type | auto | auto (prova tutti i tipi), RS41, DFM, M10/M20, iMet54: vedi sotto |
| | 2 | Frequency preset | — | apre l'elenco dei preset di `presets.txt` |
| | 3 | Receiver bandwidth | 14.6 kHz | larghezza del filtro di ricezione: da 9,7 a 39 kHz. Meglio lasciare 14,6: più larga fa entrare più rumore (a 19,5 kHz le RS41 deboli si perdono); M10/M20 usano sempre almeno 23,4. In arancione quando non è 14,6. Le app di caccia non possono cambiarla |
| | 4 | Full frames | on | riceve i frame interi da 320/518 byte (vedi sotto); off = 255 byte + correzione d'errore |
| | 5 | Raw frame log | on | salva i dati grezzi ricevuti sulla SD |
| Network | 1 | Scan networks | — | elenca le reti Wi-Fi vicine (solo quando si preme); quella scelta sostituisce il nome della rete, e per una rete protetta un riquadro chiede la password |
| | 2 | Saved networks | — | la prima voce "+ save this network" salva il nome e la password della rete impostata (chiede Sì/No); sotto, le reti salvate: quella scelta sostituisce nome e password della rete (restano dopo il riavvio); Alt+D ne cancella una dall'elenco. Stanno in `wifi.txt` (vedi sotto) |
| | 3 | Wi-Fi name | — | nome della rete Wi-Fi (per esempio l'hotspot del telefono) |
| | 4 | Wi-Fi password | set | password della rete (salvata nel dispositivo, non cifrata); "change" quando è già impostata |
| | 5 | Internet via Wi-Fi | off | usa internet tramite la rete Wi-Fi qui sopra; dice "online" solo mentre un server risponde davvero, "no internet" quando nessuno risponde (dopo 3 minuti così il T-Deck si ricollega da solo) |
| | 6 | Wi-Fi power save | off | fa dormire il Wi-Fi fra un segnale del router e l'altro: consuma meno, ma con alcuni router il collegamento smette di funzionare |
| | 7 | Launch predictions | 24 h | fin dove la scheda Launches cerca i lanci: 12 h, 24 h, 3, 5, 7 giorni |
| | 8 | Hunting apps | off | collegamento alle app: spento, Bluetooth, Bluetooth + Wi-Fi |
| | 9 | Callsign | — | il tuo nominativo, uno solo per tutti gli invii (salvato in maiuscolo, es. `IT9ABC-9`) |
| | 10 | APRS passcode | set | il passcode APRS-IS del tuo nominativo, serve per radiosondy.info e aprs.fi; "change" quando è già impostato. È un numero calcolato dal nominativo: lo trovi con un calcolatore di passcode APRS (ne esistono molti online) o te lo dà il servizio APRS. Serve un nominativo di radioamatore |
| | 11 | SondeHub upload | awaiting approval | **sospeso**: SondeHub non ha ancora approvato questo firmware, quindi la voce non si può accendere; premendola si legge come aiutare ([SondeHub](docs/SONDEHUB.md)) |
| | 12 | SondeHub JSON log | on | tiene ogni frame ricevuto nel formato di SondeHub in `sondehub/<seriale>.json` sulla SD (circa 2 MB per ora di ricezione), da mandarmi per l'approvazione ([come](docs/SONDEHUB.md)) |
| | 13 | radiosondy.info upload | off | invia a radiosondy.info le sonde ricevute come oggetti APRS (uno per sonda ogni 30 s); stessa domanda |
| | 14 | aprs.fi upload | off | invia la posizione della tua stazione ad aprs.fi (rete APRS-IS principale, server europei), con l'icona scelta sotto; stessa domanda. Solo la stazione: le sonde vanno a radiosondy.info |
| | 15 | aprs.fi comment | on | aggiunge un commento secondo la situazione: a caccia di una sonda (mobile), in ricezione (fissa), o in cerca di sonde. Testi in `aprsfi.txt`, vedi sotto |
| | 16 | Only sondes in flight | on | invia solo le sonde viste in volo; spento, anche una sonda a terra |
| | 17 | Send my position | off | dove si condivide la posizione della tua stazione: off, all (radiosondy.info e aprs.fi), solo radiosondy, solo aprs.fi. La scelta SondeHub compare in grigio finché SondeHub non approva il firmware. aprs.fi non riceve altro, quindi il suo invio richiede all o aprs.fi qui. Con all la tua posizione finisce anche nei file JSON per SondeHub |
| | 18 | Station | mobile | mobile: compare come auto inseguitrice; fixed: un ricevitore (su radiosondy.info e aprs.fi sempre l'icona della parabola `` /` ``: radiosondy.info elenca così i ricevitori). La stazione mobile invia anche velocità, direzione e quota |
| | 19 | Mobile icon | person | icona della stazione mobile su radiosondy.info e aprs.fi: persona `/[`, auto `/>`, oppure `/(` (usata da altri ricevitori di radiosonde) |
| | 20 | Mobile update | 60 s | ogni quanto si invia la posizione della stazione mobile: 30 s, 60 s, 2, 5, 10, 15 min (su aprs.fi al massimo una volta al minuto) |
| | 21 | Fixed update | 30 min | ogni quanto si invia la posizione della stazione fissa: 10, 30, 60 min |
| | 22 | GPS fix wait | 1 min | la tua posizione si condivide solo dopo che il GPS ha avuto un fix buono (3D, almeno 5 satelliti, HDOP fino a 3) per questo tempo senza interruzioni: 30 s, 1, 2, 5 min. I primi fix dopo l'accensione possono essere lontani dal vero |
| | 23 | Min distance | off | stazione mobile: nessuna nuova posizione finché non si è spostata almeno di tanto dall'ultima inviata: off, 200 m, 500 m, 1 km (un nuovo commento o icona parte comunque) |
| Display | 1 | Screen brightness | 100% | luminosità dello schermo: 100%, 75%, 50%, 25%, 12% (di notte) |
| | 2 | Screen timeout | 5 min | dopo quanto tempo senza tasti lo schermo si spegne o si attenua: sempre acceso, 1, 5, 10 min |
| | 3 | Screen dimming | screen off | cosa succede allo scadere del tempo: screen off lo spegne; light, medium, strong lo lasciano acceso ma attenuato (strong il più scuro) |
| | 4 | Theme | Desert | colori: Desert, Olive, Navy, Sun (alto contrasto per il sole) |
| | 5 | Keyboard light | Alt+B | promemoria della combinazione per la luce della tastiera |
| | 6 | Touch screen | on | attiva o disattiva il touch |
| | 7 | Auto lock | off | blocca tasti e touch dopo un tempo senza usarli: off, 1, 2, 5 min (si sblocca tenendo premuta la trackball 3 s) |
| | 8 | Trackball speed | fast | quanto deve girare la pallina per un passo: fast, medium, slow |
| System | 1 | Volume | 7 | volume di allarmi, segnale di avvicinamento e conferme: 0 muto, 9 il massimo |
| | 2 | Frame volume | 7 | volume del suono di ogni frame ricevuto, su qualsiasi pagina: 0 muto, 9 il massimo |
| | 3 | Frame sound | soft tick | suono di ogni frame: un tic leggero, oppure un bip più acuto quanto più il segnale è forte |
| | 4 | Time zone | UTC+1 | fuso orario (ora solare), scelto da un elenco con alcune città per ogni fuso, da UTC-12 a UTC+14 |
| | 5 | Daylight saving | auto | ora legale: off, on (+1 ora), auto (date europee: ultima domenica di marzo e di ottobre) |
| | 6 | Web interface | off | usare il T-Deck dal browser del telefono o del PC (vedi sotto); acceso, mostra l'indirizzo da aprire |
| | 7 | Debug server | off | pagine di controllo remoto per le prove e per segnalare problemi (vedi sotto); acceso, mostra "on, /debug" |
| | 8 | Web password | default | password dell'interfaccia web e delle pagine di debug (predefinita `radiosonde`, nome utente `tdeck`): scrivine una nuova, `-` torna alla predefinita |
| | 9 | About | — | versione, autore e link al progetto |

**Timeout dello schermo:** a schermo spento o attenuato il T-Deck continua a ricevere, registrare e **suonare**. Il primo tasto o tocco riporta soltanto lo schermo alla luminosità normale, senza fare altro; gli allarmi lo riaccendono da soli. Light, medium e strong riducono la luminosità scelta a metà, a un quarto e a un ottavo.

<p align="center">
  <img src="docs/img/themes.png" width="640"><br>
  <i>I quattro temi: Desert, Olive, Navy, Sun</i>
</p>

## Mappa offline
Il T-Deck legge la mappa dalla scheda SD, in tessere PNG da 256 pixel nella cartella `/maps/<zoom>/<x>/<y>.png`. I server pubblici di OpenStreetMap non permettono di scaricare le tessere in massa, quindi il progetto include uno script che le **disegna sul tuo PC** dai dati OpenStreetMap. Lo script è nella cartella `tools/mappack` di questo repository: si scarica tutto con il pulsante **Code**, poi **Download ZIP**, in alto in questa pagina.

Dove una tessera manca, il T-Deck mostra ingrandita quella del livello inferiore: conviene una mappa "a strati", con poco dettaglio ovunque e molto dettaglio solo dove cacci.

### Cosa serve
- **Docker** (Docker Desktop su Windows o Mac), avviato;
- **Python 3** e, consigliato, Pillow (`pip install pillow`);
- circa 10 GB liberi durante la preparazione (per una nazione come l'Italia).

### Passi
1. Copia `tools/mappack/sicilia.json` con un nuovo nome e modificalo per la tua zona:
   - in `sources` metti il nome dell'estratto [Geofabrik](https://download.geofabrik.de) della tua nazione (per esempio `italy`, `germany`, `france`);
   - in `layers` metti, per ogni strato, il riquadro `[sud, ovest, nord, est]` in gradi e gli zoom. Esempio: mondo 0-2, continente 3-7, nazione 8-11, la tua regione 12-14.
2. Controlla quante tessere verranno create:
   ```bash
   python tools/mappack/make_maps.py tools/mappack/mia_zona.json --dry-run
   ```
3. Genera la mappa (la prima volta scarica i dati e può richiedere da qualche minuto a un paio d'ore):
   ```bash
   python tools/mappack/make_maps.py tools/mappack/mia_zona.json
   ```
4. Copia la cartella generata (`mapwork/maps`) nella radice della SD con il nome `maps`: devono esserci `/maps/0`, `/maps/1`, … Riavvia il T-Deck.

Indicazione: con l'esempio della Sicilia (mondo, Europa, Italia, Sicilia fino allo zoom 14) si ottengono circa 30.000 tessere e 130 MB.

| Zoom | Larghezza dello schermo | Uso |
|---|---|---|
| 0-2 | mondo | sfondo |
| 3-7 | da 4.000 a 300 km | continente, nazione |
| 8-11 | da 150 a 19 km | regione, viaggio |
| 12-14 | da 10 a 2,4 km | strade, sterrati, ultimo tratto |

I dati della mappa sono © OpenStreetMap contributors (ODbL).

## App di caccia sul telefono
Il T-Deck può fare da ricevitore per le app già in uso, senza modificarle. Voce **Hunting apps** (scheda Network):
- **Bluetooth:** il T-Deck compare come `MySondyGO-XXXX` e parla il protocollo MySondy GO 4.1. Funziona con **MySondyGO**, **Hunter Tracker** (ricevitore MySondy) e **TrovaLaSonda**: cercalo nell'app tra i dispositivi Bluetooth. Dall'app si può cambiare la frequenza.
- **BT + Wi-Fi:** in più, sulla rete Wi-Fi impostata (scheda Network), il T-Deck si comporta come un ricevitore **TTGO rdz** (`http://<ip>/live.json` e porta TCP 14570), per le app che lo supportano.

In alto compare `BT`: grigio in attesa, verde con un'app collegata.

> Il collegamento con le app non invia nulla a internet, ma **le app possono inviare i dati a SondeHub per conto loro**: controlla le impostazioni dell'app.

## Internet tramite l'hotspot del telefono
Con **Internet via Wi-Fi** attiva (scheda Network) e il T-Deck collegato all'hotspot del telefono:
- ora esatta da internet e aggancio del GPS più rapido;
- pagina **Nearby** con le sonde vicine, gli atterraggi e i prossimi lanci;
- posizione della sonda da SondeHub quando la radio non la sente (scheda "SONDE (NET)"). SondeHub chiede di non interrogare di continuo le sue API: le sonde vicine si leggono ogni 5 minuti, ogni minuto solo con l'elenco Near aperto (sul T-Deck o nella pagina web) o seguendo una sonda che la radio non sente; la previsione della sonda seguita si chiede ogni 3 minuti; una sonda seguita ferma da 10 minuti (atterrata, o non la sente più nessuno) torna a ogni 5 minuti e la sua previsione non si chiede più;
- previsione di atterraggio di SondeHub sulla mappa, che resta salvata anche se internet cade.

Qui il T-Deck **legge soltanto**: invia dati a SondeHub, radiosondy.info o aprs.fi solo se accendi il loro invio (vedi [Invio dei dati](#invio-dei-dati-sondehub-radiosondyinfo-aprsfi)). Per chiedere le sonde vicine comunica la tua posizione arrotondata a circa 10 km. Senza internet tutto il resto funziona normalmente.

## Interfaccia web
La voce **Web interface** (scheda System, spenta di default) permette di usare il T-Deck dal browser di un telefono o di un PC sulla stessa rete Wi-Fi: apri `http://<ip>/` (l'indirizzo è nelle impostazioni, utente `tdeck`, password `radiosonde` se non l'hai cambiata, voce **Web password**). È fatta per usare il T-Deck come stazione remota: tutto quello che fanno i tasti e lo schermo lo fa anche la pagina. Come accenderla la prima volta: [Wi-Fi e pagina web, passo per passo](#wi-fi-e-pagina-web-passo-per-passo). Ha nove pagine:
- **Hunt:** la sonda (seriale, quota, velocità verticale e orizzontale, satelliti, batteria, temperatura, kill timer, ozono), distanza, direzione ed elevazione da te, la bussola (girata secondo la tua direzione mentre ti muovi, come **AHEAD** sul T-Deck), segnale, frame decodificati ed età dell'ultimo frame, la previsione di atterraggio con il pulsante **Navigate** (Google Maps) e la quota sotto la quale si perderà il segnale. I pulsanti: **To sonde** / **To landing** scelgono dove puntano distanza e freccia (il tasto `n`); **Target** scrive un punto (il tasto `t`); **KML** crea il file del percorso e lo scarica; **Reset** azzera contatori, posizione ricevuta e previsione (un bersaglio scritto resta); **Sound off** zittisce il T-Deck. Si aggiorna ogni 2 secondi;
- **Map:** tu, la sonda, il suo percorso e le previsioni di atterraggio su una mappa OpenStreetMap, presa da internet dal telefono o dal PC (le mappe offline della SD si vedono solo sullo schermo del T-Deck); il T-Deck manda solo i punti. Come il tasto `c` del T-Deck, **Both** tiene tutto in vista (tu, la sonda, il percorso e le previsioni), **Me** e **Sonde** tengono quel punto al centro; con **follow** spuntato la mappa continua a farlo (trascinarla lo ferma). Un clic sulla sonda apre i suoi link su **SondeHub** e **radiosondy.info** e il pulsante **Navigate**. Il percorso che SondeHub prevede per la sonda seguita è una linea tratteggiata;
- **Radio:** frequenza (scritta o a passi di 1 e 10 kHz), tipo di sonda, polarità, banda del ricevitore e livello del rumore, avvio e arresto della scansione con lo spettro dal vivo 400-406 MHz e i canali trovati, l'elenco dei preset con la ricerca, **Auto (no preset)**, **Next**, **Save current** (la frequenza in uso, con un nome), **Add European sites** e un cestino su ogni preset per cancellarlo;
- **SondeHub:** i quattro elenchi della pagina **Nearby** del T-Deck: **Near**, **Landed** (ultime 3 h, 12 h, 24 h o 7 giorni), **Launches** (prossimi lanci dei siti vicini, con l'atterraggio previsto) e **Search** per seriale. Un clic su una sonda apre un riquadro con **Follow and tune** (la segue e si sintonizza) o **Stop following**, il link a SondeHub e **Navigate**; nei dettagli si può rendere l'atterraggio il tuo bersaglio. Serve **Internet via Wi-Fi** acceso;
- **Signal:** il segnale adesso e il suo picco degli ultimi 4 secondi, due volte al secondo, l'ultimo frame e la distanza: la pagina per gli ultimi metri. **Tone** fa sentire nel browser un tono che segue il segnale;
- **GPS:** fix, satelliti usati, HDOP e PDOP, il segnale di ogni satellite in vista, posizione, quota, velocità e rotta, ora e aiuto al GPS;
- **Info:** le righe della pagina Info del T-Deck (radio, contatori, ultimo frame, stato della sonda, SondeHub);
- **Settings:** tutte le voci delle quattro schede, con gli stessi valori e le stesse scelte del T-Deck. Accendere un invio chiede "Are you sure?" sia sul T-Deck sia nel browser; la voce **SondeHub upload** spiega come aiutare ad avere l'approvazione. Un messaggio del T-Deck (per esempio perché una voce non cambia) compare in fondo alla pagina. **Saved networks** ha un cestino su ogni rete, **Scan networks** chiede la password della rete scelta;
- **Files:** i file e le cartelle della scheda SD, ognuno con i pulsanti per **scaricarlo**, **rinominarlo** ed **eliminarlo** (l'eliminazione chiede prima e non si può annullare; una cartella solo se vuota), e **Upload files** (oppure trascinali sulla pagina) per copiare file dal telefono o dal PC sulla scheda, nella cartella aperta. Per esempio, se usi il Launcher, un firmware nuovo `.bin` in `/downloads`: poi lo installi dal Launcher, senza togliere la scheda. Mentre un file si copia, in un senso o nell'altro, il T-Deck non riceve: meglio dopo la caccia.

In alto il lucchetto blocca e sblocca i tasti del T-Deck (la pagina funziona anche con i tasti bloccati) e il menu ha **About** e **Restart the T-Deck**. Quello che cambi dal browser cambia anche sul T-Deck, e viceversa. La pagina usa i colori del tema scelto sul T-Deck (scheda Display, voce Theme) e riempie sempre la finestra, dal telefono al monitor grande, senza scorrere: scorrono solo gli elenchi lunghi (preset, file, e le impostazioni sul telefono in orizzontale) dentro il loro riquadro.

La password è solo un lucchetto contro un'occhiata veloce su una rete condivisa, non una vera protezione: usa l'interfaccia web solo su reti tue (per esempio l'hotspot del telefono).

## Server di debug
La voce **Debug server** (scheda System, spenta di default) accende le pagine per le prove e per segnalare problemi a `http://<ip>/debug` (stessi utente e password dell'interfaccia web, stessi colori del tema), con i pulsanti:
- **Screen:** lo schermo del T-Deck, che si può cliccare come il touch, con le frecce, Invio, Indietro, Home e i tasti più usati;
- **Status** e **Log:** lo stato del T-Deck e i suoi ultimi messaggi, utili per segnalare problemi.

File e riavvio stanno nell'interfaccia web.

Le pagine chiedono una **password**: `radiosonde` finché non la cambi (voce **Web password**). Il nome utente è sempre `tdeck`. È solo un lucchetto contro un'occhiata veloce su una rete condivisa, non una vera protezione: accendi le pagine solo quando servono, e solo su reti tue (per esempio l'hotspot del telefono). I dati `/live.json` per le app di caccia (Hunting apps: BT + Wi-Fi) non chiedono password.

## Frame completi e sensore d'ozono
La radio SX1262 del T-Deck normalmente chiude un pacchetto a 255 byte, mentre un frame RS41 è lungo 320 byte (518 con i dati di sensori aggiuntivi). Con **Full frames** (scheda Radio) (accesa di default) il T-Deck usa il metodo per i pacchetti lunghi descritto da Semtech (nota applicativa AN1200.53): mentre il frame arriva, la fine del pacchetto viene spostata in avanti, così **il frame arriva intero**.

- **Ricezione migliore col segnale debole e disturbato.** Con il frame intero la correzione d'errore ripara fino a 12 byte sbagliati per metà frame, contro 3 quando arrivano solo 255 byte. Nelle prove al banco con una sonda simulata e molto rumore, il T-Deck ha decodificato **il 97% dei frame con Full frames, contro il 26%** senza. Col segnale buono i due metodi sono pari.
- **Sonde con sensore d'ozono** (Vaisala OIF411, frame estesi da 518 byte): la pagina **Info** mostra la pressione parziale dell'ozono in mPa, la corrente della cella e la temperatura della pompa; la pagina di caccia mostra "O3" accanto ai dati della sonda; il file CSV ha la colonna `o3_mpa`. Il valore in mPa è una **stima**: usa la portata nominale della pompa, non il foglio di calibrazione del singolo sensore.
- Se un frame viene notato troppo tardi per essere esteso (per esempio mentre la mappa carica molte tessere), il T-Deck usa per quel frame il metodo a 255 byte: non si perde niente rispetto a prima.
- Mentre riceve un frame il T-Deck non fa altro, per non disturbare la radio: con i frame che arrivano ogni secondo lo schermo può reagire un po' più lentamente.

## Tipi di sonda e preset
**Tipi di sonda.** Oltre alla Vaisala RS41 il T-Deck decodifica:

| Tipo | Modelli | Cosa si legge |
|---|---|---|
| DFM | Graw DFM-06, DFM-09, DFM-17 | posizione, velocità, ora, numero di serie, modello, batteria (DFM-09/17) |
| M10/M20 | Meteomodem M10, M10+, M20 | posizione, velocità, ora, numero di serie, modello, batteria, satelliti (M10) |
| iMet54 | InterMet iMet-54, iMet-50 | posizione, ora del giorno, numero di serie, modello; velocità calcolate dalle posizioni |

Voce **Sonde type** (scheda Radio) (o tasto `y`):
- **auto** (predefinito): il ricevitore prova a turno tutti i tipi, con le due polarità, finché decodifica un frame; poi resta su quel tipo finché il segnale non si perde. Se il preset in uso indica un tipo, usa subito quello;
- **RS41**, **DFM**, **M10/M20** o **iMet54**: solo quel tipo, il modo più rapido quando sai già cosa volerà.

Quando segui una sonda dalla pagina Nearby, il tipo viene preso da SondeHub. Il numero di serie delle DFM arriva ogni qualche frame: per i primi 20-30 secondi la sonda compare come "DFM".

**Preset.** Le frequenze stanno nel file **`presets.txt`** sulla scheda SD, che il T-Deck crea la prima volta con le istruzioni dentro e circa **90 preset di siti di lancio europei** già pronti (alcuni siti hanno più frequenze, e c'è anche Tunisi), divisi per paese (frequenza e tipo delle sonde sentite su SondeHub, novembre 2026). Il T-Deck legge i preset sempre da questo file e lo modifica solo quando glielo chiedi (tasto `a`, Alt+D, "+ add the European launch sites"), tenendo le tue modifiche: se un sito cambia frequenza, basta correggere la sua riga. Una riga per preset:

```
[Sicily]
404.500  RS41  Trapani Birgi
402.000  DFM   Tunis (DFM-17)
[France]
403.000  M10   Trappes
[Saved]
405.100  AUTO
```

- frequenza in MHz, tipo (`AUTO`, `RS41`, `DFM`, `M10`, `M20` o `iMet54`), nome (facoltativo); `[Nome]` inizia un gruppo (paese, regione); `#` inizia un commento; fino a 200 preset (puoi aggiungere i tuoi);
- la voce **Frequency preset** (scheda Radio) (o il tasto `j` da qualsiasi pagina) apre l'elenco: scrivi parte di un nome, di un paese o una frequenza (`trapani`, `germany`, `404.5`) e restano solo le righe che corrispondono; col trackball a sinistra/destra salti da un paese all'altro; scegli con Invio, il trackball o un tocco e il T-Deck si sintonizza;
- il preset scelto resta in uso, anche dopo il riavvio, finché non scegli **auto** (prima riga dell'elenco): allora non c'è nessun preset e il ricevitore trova da solo il tipo di sonda;
- il tasto `p` passa al preset successivo dello stesso paese, il tasto `a` salva frequenza e tipo attuali sotto `[Saved]` con il nome che si scrive (un nome già usato diventa "nome 2", "nome 3"…; il preset in uso non viene mai modificato); nell'elenco, con la ricerca vuota, Alt+D cancella il preset selezionato dopo una conferma;
- l'ultima riga dell'elenco, **"+ add the European launch sites"**, aggiunge i siti a un file già esistente, senza doppioni: un sito di cui hai corretto la frequenza non viene aggiunto di nuovo;
- puoi anche modificare il file sul PC: togli la SD, cambialo, rimettila.

## Invio dei dati: SondeHub, radiosondy.info, aprs.fi
Ogni invio è **spento di default**, e accenderlo chiede "Are you sure?" con **No** già scelto. Servono il Wi-Fi (scheda Network) e il tuo **Callsign**; radiosondy.info e aprs.fi anche l'**APRS passcode** del nominativo.

| Servizio | Cosa riceve | Voci (scheda Network) |
|---|---|---|
| radiosondy.info | le sonde ricevute (un oggetto APRS per sonda ogni 30 s) e la tua stazione | radiosondy.info upload |
| aprs.fi | solo la tua stazione, con un commento secondo la situazione (`aprsfi.txt`) | aprs.fi upload, aprs.fi comment |
| SondeHub | **sospeso**: SondeHub chiede voli interi di ogni tipo di sonda prima di approvare un programma nuovo. Nel frattempo i frame restano sulla SD (**SondeHub JSON log**): vedi [SondeHub: serve aiuto](docs/SONDEHUB.md) | SondeHub JSON log |

- **La posizione della tua stazione** va solo dove dice **Send my position** (off, all o un solo servizio), solo dopo un fix GPS stabile (**GPS fix wait**), con velocità e direzione se mobile; **Min distance** salta gli aggiornamenti finché una stazione mobile non si sposta.
- Di default si inviano solo le sonde viste in volo (**Only sondes in flight**); i seriali dei simulatori del banco mai.
- L'identificativo APRS dell'apparato è `APTKRS`.

## SondeHub: serve aiuto
SondeHub abilita un programma nuovo solo dopo aver controllato **voli interi di ogni tipo di sonda** che decodifica. Dalla Sicilia ricevo quasi solo RS41 e DFM-17: voli di **DFM, M10/M20 e iMet-54** (e altre RS41) da altri posti aiuterebbero molto.

Con **SondeHub JSON log** acceso (lo è di default) il T-Deck tiene ogni frame ricevuto in `sondehub/<seriale>.json` sulla SD. Come mandare i file: **[docs/SONDEHUB.md](docs/SONDEHUB.md)**.

## File sulla scheda SD
Tutti i file del T-Deck stanno nella cartella **`/t-deck-sonde-tracker/`**; restano fuori solo le tessere della mappa in `/maps/` e i firmware in `/downloads/`, condivisi con altri programmi.

| File | Contenuto |
|---|---|
| `sonde/<seriale>.csv` | percorso della sonda, una riga per pacchetto (si apre con un foglio di calcolo; `o3_mpa` per le sonde con l'ozono) |
| `sonde/<seriale>.kml` | percorso per Google Earth, creato con il tasto `k` |
| `log.txt` | registro dei pacchetti ricevuti |
| `raw.txt` | dati grezzi (Raw frame log), per analisi |
| `presets.txt` | preset di frequenza (vedi sopra) |
| `wifi.txt` | reti Wi-Fi salvate, una per riga `nome,password` (`nome,` per una rete aperta); si può anche scrivere a mano. La prima virgola separa nome e password, quindi il nome non può contenerne; gli spazi alle estremità si tolgono. Le password sono in chiaro: tienilo presente se presti la scheda |
| `aprsfi.txt` | commenti inviati ad aprs.fi, una riga `chiave = testo`: `chase` (stazione mobile che riceve una sonda), `receive` (stazione fissa), `idle` (nessuna sonda ricevuta via radio), e `chase_nosite` / `receive_nosite` senza un preset in uso. `{serial}` è la sonda, `{site}` il nome del preset di frequenza in uso. Creato con i testi inglesi alla prima accensione; modificalo per cambiarli (fino a 59 caratteri, niente `|` o `~`) |
| `sondehub/<seriale>.json` | ogni frame ricevuto nel formato di SondeHub, una riga per frame (SondeHub JSON log): i dati della sonda, il tuo nominativo, e la tua posizione solo se Send my position è "all". Vedi [docs/SONDEHUB.md](docs/SONDEHUB.md) |
| `gpsdb.ubx` | dati dei satelliti salvati dal GPS, per riagganciarsi prima |
| `/maps/` | tessere della mappa (fuori dalla cartella del T-Deck) |
| `/downloads/` | firmware da installare con il Launcher (fuori dalla cartella del T-Deck) |

Le quote nel KML sono quelle trasmesse dalla sonda (ellissoidiche): in Google Earth il percorso appare qualche decina di metri più in alto.

## Come è stato provato
<img src="docs/img/bench.jpg" width="420" align="right">

Ogni versione è stata provata prima **al banco** e poi, per la RS41, **con sonde vere**:
- **RS41:** al banco con una sonda RS41 recuperata, poi sul campo seguendo i voli reali lanciati da Trapani (frame interi letti fino a circa -113 dBm, a oltre 45 km di distanza).
- **DFM, M10/M20, iMet-54** (e anche la RS41): **sonde simulate**. Una HackRF (PortaPack) collegata al T-Deck con un cavo e attenuatori trasmette frame costruiti come quelli veri, con seriali finti, polarità normale e invertita e vari livelli di rumore. Ogni versione passa una sessione automatica di 20 prove: seriale, modello e posizione devono essere sempre giusti. Nella foto: la PortaPack trasmette una RS41 con sensore d'ozono simulata e il T-Deck la decodifica.
- **Lettere N E S W** della bussola in movimento e **memoria** del bersaglio manuale e della previsione di SondeHub dopo lo spegnimento: verificate sul campo.
- **Invii:** radiosondy.info (sonde e stazione) e aprs.fi (stazione) su voli veri di RS41 a novembre 2026, con posizioni, quote e ore uguali a quelle delle altre stazioni; SondeHub con dati di prova che controlla ma non salva.
- **Aggancio rapido del GPS** con i dati dei satelliti salvati sulla SD: verificato spegnendo e riaccendendo il T-Deck, anche dopo un viaggio di un centinaio di chilometri.

Per questi tipi di sonda manca ancora la prova con voli reali, che dove abito sono rari. **Le prove sul campo e i commenti di chi usa il firmware sono molto apprezzati:** si possono lasciare nella sezione **Issues** di GitHub (tipo di sonda, frequenza, distanza, cosa ha funzionato e cosa no).
<br clear="right">

## Funzioni in sviluppo e da verificare
Complete nel firmware, ma **non ancora provate sul campo**:
- collegamento con **MySondyGO, Hunter Tracker e TrovaLaSonda** (Bluetooth e Wi-Fi);
- **scansione** (ricerca continua): funziona al banco (23 novembre 2026, RS41 simulata), resta da provare con una sonda vera;
- precisione della **previsione di atterraggio** su più voli. Il 21 novembre 2026 (una RS41 sentita solo da 25 km in su, quindi senza i venti bassi) la previsione del T-Deck sbagliava di 6-17 km sopra i 5 km, di 2 km a 3,8 km, di meno di 1 km sotto i 2,5 km e di **48 m** a 570 m; quella di SondeHub di 1-6 km in alto e di 500 m alla fine. Il T-Deck mostra la previsione di SondeHub (con internet) finché alla sua mancano i venti sotto la sonda, e la sua sotto i 2500 m dal suolo;
- **sensore d'ozono**: la decodifica è stata scritta sul formato del sensore OIF411 e provata con sonde simulate, ma i voli con l'ozono sono rari e manca la prova con una sonda vera;
- **decoder DFM, M10/M20 e iMet-54** con sonde vere (provati al banco con sonde simulate, vedi sopra);
- **invio a SondeHub**: sospeso finché SondeHub non approva il firmware, e per farlo servono voli interi di ogni tipo di sonda: [serve aiuto](docs/SONDEHUB.md).

## Limiti noti
- Solo sonde **Vaisala RS41**, **Graw DFM**, **Meteomodem M10/M20** e **InterMet iMet-54**: niente Vaisala RS92, iMet-1/4 e altri tipi rari in Europa. Dei sensori aggiuntivi delle RS41 (XDATA) viene decodificato solo il sensore d'ozono OIF411; gli altri compaiono come dati grezzi nella pagina Info. La temperatura di DFM, M10/M20 e iMet-54 non è ancora mostrata.
- Nessuna bussola magnetica: da fermo la direzione è riferita al nord.
- La tastiera gestisce da sola `Alt`+`B` (la sua luce) e non segnala quando un tasto viene rilasciato: per questo il blocco dello schermo è sulla trackball.
- La previsione di atterraggio considera il terreno piatto alla tua quota.

## Glossario
- **Fix:** il GPS ha calcolato la posizione. 2D senza la quota, 3D con la quota.
- **HDOP / PDOP:** quanto sono ben distribuiti i satelliti in cielo. Più il numero è basso, più la posizione è precisa (sotto 2 è buona).
- **dBm:** la forza del segnale radio. È un numero negativo: -80 è forte, -115 è al limite.
- **Polarità:** alcune sonde arrivano con i bit invertiti. In automatico il T-Deck prova entrambi i modi.
- **APRS-IS:** la rete internet dei radioamatori che usano APRS. radiosondy.info e aprs.fi ricevono i dati da lì.
- **Passcode APRS:** il numero che apre APRS-IS al tuo nominativo.
- **Kill timer:** il tempo dopo cui la sonda smette di trasmettere da sola.
- **XDATA:** i dati dei sensori in più appesi alla RS41, per esempio quello dell'ozono.
- **Frame:** un pacchetto di dati della sonda. Ne arriva circa uno al secondo.

## Compilare dai sorgenti
1. Installa [VS Code](https://code.visualstudio.com/) con l'estensione **PlatformIO IDE**, oppure `pip install platformio`.
2. Apri la cartella del progetto e compila:
   ```bash
   pio run
   ```
   Al primo avvio PlatformIO scarica toolchain e librerie. L'immagine completa da installare viene creata in `release/`.
3. Per scrivere direttamente sul T-Deck collegato via USB: `pio run -t upload`.

## Crediti e licenza
Autore: **N1k0droid (IT9KVB)** — https://github.com/N1k0droid

Il codice è stato scritto con l'aiuto di strumenti di programmazione assistita (coding agent), sotto la mia guida, e provato da me sul dispositivo reale.

Grazie a:
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI), [PNGdec](https://github.com/bitbank2/PNGdec), [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino), [QRCode](https://github.com/ricmoo/QRCode);
- il protocollo MySondy GO e il progetto [rdz_ttgo_sonde](https://github.com/dl9rdz/rdz_ttgo_sonde), per la compatibilità con le app;
- [rs1729/RS](https://github.com/rs1729/RS) per la documentazione del formato RS41;
- [SondeHub](https://sondehub.org), [radiosondy.info](https://radiosondy.info) e [aprs.fi](https://aprs.fi) per i dati e le mappe che condividono;
- [Launcher](https://github.com/bmorcelli/Launcher) di bmorcelli ed [ESP Web Tool](https://github.com/spacehuhntech/espwebtool) di Spacehuhn, per i loro strumenti che permettono e facilitano l'installazione del firmware;
- dati della mappa © [OpenStreetMap](https://www.openstreetmap.org/copyright) contributors, [OpenMapTiles](https://openmaptiles.org), [Planetiler](https://github.com/onthegomap/planetiler), [TileServer GL](https://github.com/maptiler/tileserver-gl).

Licenza: **GNU GPL v3.0 o successiva** (vedi [LICENSE](LICENSE)). Puoi usare, modificare e ridistribuire il codice, mantenendo la stessa licenza e citando l'origine.

Ti piace T-Deck SondeTracker? Una ⭐ al repository e un follow aiutano il progetto a crescere.
