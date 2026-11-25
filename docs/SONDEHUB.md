# SondeHub: help wanted

[Italiano più sotto](#italiano)

## Why
[SondeHub](https://sondehub.org) collects radiosonde data from receivers all over the world. Before it accepts data from a new software, its team checks **whole flights of every sonde type** that software decodes. Until T-Deck SondeTracker is approved, its upload to SondeHub is **suspended** (the "SondeHub upload" row is greyed out).

From Sicily I receive almost only **RS41** and **DFM-17** sondes. Flights of **DFM (06, 09, 17), Meteomodem M10/M20 and InterMet iMet-54** received elsewhere would help a lot, and more RS41 flights are welcome too.

## What the T-Deck keeps
With **SondeHub JSON log** on (Network tab, on by default) every received frame with a position is written to the SD card:

- one file per sonde: `/t-deck-sonde-tracker/sondehub/<serial>.json` (for example `/t-deck-sonde-tracker/sondehub/X1232383.json`);
- one line per frame, in the format SondeHub uses for uploads: sonde serial, type, time, position, speeds, temperature, battery, frequency, signal level;
- your **callsign** (Network tab, Callsign row);
- your **station position** only if **Send my position** is set to **all**;
- about 2 MB per hour of reception.

The serial numbers of the bench simulators are never written.

## How to get the files
- **From the SD card:** switch the T-Deck off, put the card in a PC and copy the `t-deck-sonde-tracker/sondehub` folder.
- **Over Wi-Fi:** turn on **Web interface** (System tab), open `http://<ip>/` (the address is in the settings; user `tdeck`, password `radiosonde` unless you changed it), page **Files**, button **sondehub**, then the download arrow of each file.

A useful file covers a flight from launch (or from when you first heard it) to landing, or as much of it as you received.

## How to send them
1. Put the files in a `.zip` archive. Name it with the sonde type, for example `M20-sondehub.zip`.
2. Open a new issue in this repository with the **SondeHub data** template: [new issue](https://github.com/N1k0droid/t-deck-sonde-tracker/issues/new?template=sondehub-data.md).
3. Attach the `.zip` and write the sonde type, the date and roughly where you received it.

Without a GitHub account, send the same by email to **it9kvb@gmail.com** (subject: `SondeHub data`).

I use the files only to ask SondeHub for the approval: I send them to the SondeHub team, and to nobody else. If you do not want your callsign or your position in them, say so in the issue or in the email: I will remove them before sending.

Thank you!

---

## Italiano

## Perché
[SondeHub](https://sondehub.org) raccoglie i dati delle radiosonde dai ricevitori di tutto il mondo. Prima di accettare i dati di un programma nuovo, il suo team controlla **voli interi di ogni tipo di sonda** che quel programma decodifica. Finché T-Deck SondeTracker non è approvato, il suo invio a SondeHub è **sospeso** (la voce "SondeHub upload" è grigia).

Dalla Sicilia ricevo quasi solo sonde **RS41** e **DFM-17**. Voli di **DFM (06, 09, 17), Meteomodem M10/M20 e InterMet iMet-54** ricevuti altrove aiuterebbero molto, e anche altri voli di RS41 sono benvenuti.

## Cosa tiene il T-Deck
Con **SondeHub JSON log** acceso (scheda Network, acceso di default) ogni frame ricevuto con una posizione viene scritto sulla SD:

- un file per sonda: `/t-deck-sonde-tracker/sondehub/<seriale>.json` (per esempio `/t-deck-sonde-tracker/sondehub/X1232383.json`);
- una riga per frame, nel formato che SondeHub usa per gli invii: seriale, tipo, ora, posizione, velocità, temperatura, batteria, frequenza, livello del segnale;
- il tuo **nominativo** (scheda Network, voce Callsign);
- la **posizione della tua stazione** solo se **Send my position** è su **all**;
- circa 2 MB per ora di ricezione.

I seriali dei simulatori del banco non vengono mai scritti.

## Come prendere i file
- **Dalla SD:** spegni il T-Deck, metti la scheda nel PC e copia la cartella `t-deck-sonde-tracker/sondehub`.
- **Via Wi-Fi:** accendi **Web interface** (scheda System), apri `http://<ip>/` (l'indirizzo è nelle impostazioni; utente `tdeck`, password `radiosonde` se non l'hai cambiata), pagina **Files**, pulsante **sondehub**, poi la freccia di download di ogni file.

Un file utile copre un volo dal lancio (o da quando l'hai sentito) all'atterraggio, o quanto ne hai ricevuto.

## Come mandarli
1. Metti i file in un archivio `.zip`. Chiamalo con il tipo di sonda, per esempio `M20-sondehub.zip`.
2. Apri una nuova issue in questo repository con il modello **SondeHub data**: [nuova issue](https://github.com/N1k0droid/t-deck-sonde-tracker/issues/new?template=sondehub-data.md).
3. Allega lo `.zip` e scrivi il tipo di sonda, la data e più o meno dove l'hai ricevuta.

Senza un account GitHub, manda lo stesso per email a **it9kvb@gmail.com** (oggetto: `SondeHub data`).

Uso i file solo per chiedere l'approvazione a SondeHub: li mando al team di SondeHub e a nessun altro. Se non vuoi il tuo nominativo o la tua posizione dentro, scrivilo nella issue o nell'email: li tolgo prima dell'invio.

Grazie!
