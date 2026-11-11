# Chasse aux bugs avant le premier essai sur console — ACNH Power Pockets

> **Portée** : relecture adverse de `source/main.cpp`, `source/pp_model.*`, `source/pp_ui.*`, `source/pp_strings.h`, `Makefile`, `.github/workflows/build.yml`, `tests/host/`, `tools/`, à partir du commit `f993e45` (branche `power-pockets`).
> **Références de lignes** : sauf mention « après correction », les numéros `fichier:ligne` renvoient au code **du commit `f993e45`**, là où se trouvait le défaut.
> **Moteur** : aucune zone moteur n'a été modifiée. `python3 tools/check_engine_untouched.py upstream/main` répond `ENGINE UNTOUCHED`.
> **Rien n'est commité.** Toutes les modifications sont dans l'arbre de travail.

## 0. Résumé

**Verdict : aucun chemin trouvé qui écrive autre chose que prévu dans la save.** Le vrai moteur, exécuté sur Mac contre une console simulée, n'écrit que les 160 octets des poches, les 4 champs EncryptedInt32 et les 8 champs « heal » : le fichier écrit est comparé octet par octet à un résultat calculé indépendamment.

En revanche, **le filet de sécurité avait trois trous** (S0), tous corrigés et couverts par un test :

1. le backup de départ (`session_*.dat`) était **écrasé au lancement suivant**, précisément dans le scénario où on en a besoin (deux Apply, le jeu refuse la save, on relance l'app) ;
2. la validation d'un backup avant restauration reposait, pour `main.dat` (10 Mo), sur **un seul octet de checksum** : un `backup_main.dat` endommagé ailleurs était accepté ;
3. une copie interrompue du backup de départ vers le backup courant laissait une **paire mélangée** que l'UI proposait ensuite de restaurer.

Deux blocages (S1) : un **écran noir de plusieurs minutes** au lancement après une modification du prêt (hérité du moteur, atténué par un écran d'attente), et une restauration **refusée quand la carte SD est pleine** (corrigé).

| Vérification | Avant | Après |
|---|---|---|
| `sh tests/host/run.sh` | 123 tests | **146 tests**, dont un fuzz des parseurs (3 000 fichiers) sous ASan/UBSan |
| `sh tests/host/ui_shots.sh` | 19 vérifications | **40 vérifications**, dont l'injection de pannes sur chaque hook |
| `sh tests/host/engine_bench.sh` (nouveau) | n'existait pas | **224 vérifications**, 15 scénarios, vrai `main.cpp` |
| Même banc sur le code du commit `f993e45` | — | **23 échecs** : ce sont les preuves des constats ci-dessous |
| `tools/check_engine_untouched.py` | vert | vert |

## 1. Bancs de test ajoutés

### 1.1 Banc « vrai moteur » : `tests/host/engine_bench.sh`

Il compile le **vrai** `source/main.cpp` (moteur upstream, hooks, UI) avec `-Dmain=pp_app_main`, contre une fausse plateforme Switch (`tests/host/engine/fake_platform.cpp`) :

- **savedata simulée avec journal** : les écritures restent en mémoire jusqu'à `fsFsCommit`, et sont perdues si on ferme sans commit. C'est le modèle supposé d'Horizon, pas une preuve de son comportement ;
- **carte SD simulée** : un dossier nommé `sdmc:`, ce qui fait pointer les `fopen("sdmc:/…")` de l'app et les `fsFs*` du moteur vers les mêmes fichiers ;
- **manette scriptée, horloge virtuelle** (16 ms par image, donc l'appui maintenu de 1,5 s ne coûte rien) ;
- **écran en texte** : chaque chaîne dessinée par l'UI est enregistrée, ce qui permet de vérifier quel dialogue est affiché ;
- **pannes injectables** : SD non ouvrable, création ou écriture SD échouée, écriture NAND échouée par fichier, commit échoué, `malloc` échoué, carte pleine (`RLIMIT_FSIZE`), police illisible, mode applet, montage refusé.

La save synthétique a les tailles 3.0.x (`0x74A40` et `0x9B0E90`), les 2 + 19 régions de hash de la table NHSE `REV_300` avec des hash valides, quatre champs EncryptedInt32 valides, et un champ « heal » volontairement non nul. Les oracles (Murmur3, EncryptedInt32) sont réécrits dans le banc, sans reprendre le code du moteur.

Ni SDL ni devkitPro ne sont nécessaires. Durée : environ 25 s. `PP_SRC=<dossier>` permet de compiler une autre copie de `source/`, ce qui a servi à produire les preuves sur le code d'origine.

### 1.2 Injection de pannes dans `tests/host/ui_harness.cpp`

Chaque hook du faux moteur peut échouer. Les nouvelles vérifications portent sur le nombre d'appels au moteur et sur l'état des poches après chaque panne : backup de départ impossible, « BAK », écriture NAND échouée, backup refusé, copie échouée, Doctor échoué, save en lecture seule, bouton A maintenu au lancement.

### 1.3 Fuzz des parseurs dans `tests/host/test_model.cpp`

3 000 fichiers (bruit pur, bruit dans l'alphabet de la grammaire, fichiers valides mutés) passent dans `Favorites::load`, `StackRules::build`, `loadoutRead`, `stateMarkMatches`, `parseHexId` et `searchItems`, sous AddressSanitizer et UBSan. **Aucun défaut trouvé** dans ces parseurs.

## 2. Synthèse des constats

| # | Gravité | Constat | Statut |
|---|---|---|---|
| F1 | **S0** | Le backup de départ est écrasé au lancement suivant et n'est pas restaurable après relance | Corrigé + test |
| F2 | **S0** | La validation d'un backup ne voit pas un `backup_main.dat` endommagé | Corrigé + test |
| F3 | **S0** | Copie interrompue du backup de départ : paire mélangée proposée à la restauration | Corrigé + test |
| F4 | **S1** | Écran noir de plusieurs minutes au lancement après une modification (moteur) | Atténué (écran d'attente) ; cause non corrigée |
| F5 | **S1** | Restauration et Doctor refusés quand la carte SD est pleine | Corrigé + test |
| F6 | S1 | A encore maintenu au lancement : l'écran d'erreur fatale se ferme sans être lu | Corrigé + test |
| F7 | S1 | `font.ttf` présent mais illisible : interface vide, boutons actifs | Corrigé + test |
| F8 | S2 | Récap : après « Revert this change », le focus retombe sur **Apply** | Corrigé + test |
| F9 | S2 | Un loadout de 160 octets au contenu invalide est chargé tel quel | Corrigé + test |
| F10 | S2 | Sans `items.txt`, la protection « 1/slot » disparaît sans le dire | Texte corrigé ; blocage à décider |
| F11 | S2 | Stepper de quantité : valider sans bouger peut créer une modification | Corrigé |
| F12 | S2 | Backup de départ raté : fichiers partiels laissés sur une carte déjà pleine | Corrigé + test |
| F13 | S3 | Textes faux ou trompeurs (6 chaînes, README) | Corrigé |
| F14 | S3 | Message d'erreur fatale hérité de l'upstream quand la mémoire manque | Corrigé |
| F15 | S3 | Lecture hors tampon possible des poches sur une save trop petite | Corrigé côté UI |
| F16 | S3 | Annuler le clavier de recherche remet le curseur en haut ; Récap non recadré ; loadout endommagé affiché « 0/20 » | Corrigé |
| F17 | S3 | Un ID saisi à la main et absent d'`items.txt` est accepté sans avertissement | Non corrigé, à décider |
| E1–E9 | — | Défauts du moteur hérité | Documentés, non modifiés (§ 5) |

## 3. Constats S0 — save et backups

### F1 — Le backup de départ est écrasé au lancement suivant

- **Emplacement** : `source/main.cpp:632-638` (`hooks.writeSessionBackup`, drapeau `sessionBackupDone` remis à faux à chaque lancement), `source/pp_ui.cpp:916` (ligne « Restore start-of-session backup » bloquée tant que `sessionWritten` est faux), `source/pp_ui.cpp:1025` (toute restauration réécrit d'abord `session_*.dat`).
- **Scénario** : état A (écrit par le jeu). Apply → B. Apply → C. On quitte. Le jeu refuse la save, ou les items sont faux. On relance l'app pour revenir à A.
  1. « Restore start-of-session backup » répond « Blocked: no start-of-session backup yet », alors que `session_*.dat` contient bien A.
  2. « Restore SD backup » ne rend que B, un état déjà édité. Et avant de restaurer, l'app **écrase `session_*.dat` (A) par C**.
  3. A n'existe plus nulle part sur la carte SD.
- **Pourquoi c'est le pire cas** : c'est exactement le déroulement d'un premier essai qui tourne mal, et B a toutes les chances d'être refusé par le jeu pour la même raison que C.
- **Preuve** : scénario `relaunch_keeps_pre_edit_backup` du banc. Sur le code d'origine : `session_personal.dat` diffère de A à `0x37CC8` (les poches) après la restauration du lancement 2, puis l'écran affiche « Blocked » au lancement 3. 6 vérifications échouent.
- **Correction** (`source/main.cpp:596-607` et `:685-695` après correction, `source/pp_model.cpp:545-585`) : le backup de départ devient un **backup « pre-edit »**, conservé tant que la console contient l'état que l'app a écrit en dernier.
  - Après chaque écriture console réussie (Apply, restauration, Doctor), l'app écrit dans `session_mark.txt` la signature (FNV-1a 64 bits) des deux fichiers chiffrés qu'elle vient d'écrire.
  - Au lancement, si la console contient encore cet état, c'est que **le jeu n'a pas sauvegardé entre-temps** : la paire `session_*.dat` est toujours l'état d'avant la première modification. Elle est conservée et restaurable immédiatement.
  - Si la signature diffère (le jeu a sauvegardé, ou la save a été restaurée par JKSV), la prochaine écriture prend un backup neuf.
  - Si le fichier de marque est absent ou illisible, on retombe sur l'ancien comportement (backup neuf à la première écriture). L'erreur va donc toujours du côté « on refait un backup », jamais du côté « on garde un backup périmé ».
- **Statut** : **corrigé + tests** (`relaunch_keeps_pre_edit_backup`, `game_saved_in_between`, tests unitaires de `stateSignature` et `stateMarkMatches`). **À valider par l'utilisateur** : cela change la décision initiale « backup de début de session » en « backup d'avant la première modification depuis la dernière sauvegarde du jeu ». Les textes à l'écran et le README suivent (« pre-edit backup »).

### F2 — La validation d'un backup ne voit pas un `backup_main.dat` endommagé

- **Emplacement** : `source/main.cpp:602-628`, en particulier `:620-622`.
- **Défaut** : l'invariant I8 ne vérifiait que les 4 checksums EncryptedInt32. Trois sont dans `personal.dat`. Pour `main.dat` il n'en reste qu'**un, sur un octet**, à `LOAN_OFF + 7`.
  - Un `backup_main.dat` endommagé n'importe où ailleurs (secteur SD défectueux, copie interrompue sur PC) était accepté à coup sûr.
  - Le cas critique de l'audit (§ 3.f : `backup_main.dat` créé à la bonne taille mais jamais rempli) n'était refusé qu'avec une probabilité de 255/256 une fois l'app relancée, puisque le drapeau `backupTrusted` ne vit qu'en mémoire.
- **Preuve** : scénario `backup_main_damaged`. 512 octets inversés à `0x700000` de `backup_main.dat` : le code d'origine affiche le dialogue « Restore the SD backup? » et l'aurait écrit en NAND. 2 vérifications échouent.
- **Correction** (`source/main.cpp:649-679` après correction, `pp::diffOutside`) : un backup légitime de la save chargée ne peut différer d'elle **que là où l'app écrit**. Après déchiffrement d'une copie, toute différence hors des 160 octets de poches, des 4 champs de 8 octets et des 8 champs « heal » fait refuser le backup. Lecture seule, sans allocation supplémentaire.
- **Effet de bord voulu** : un backup fait **avant la dernière sauvegarde du jeu** est refusé, avec un message qui le dit. C'était déjà le cas en pratique si le jeu régénère sa clé à chaque sauvegarde (hypothèse non vérifiée, voir § 7) ; c'est maintenant vrai dans tous les cas. Restaurer deux fichiers d'une génération plus ancienne dans une save dont les autres fichiers ont évolué n'est pas un état que le jeu a produit.
- **Statut** : **corrigé + tests** (`backup_main_damaged`, `backup_main_left_empty` au deuxième lancement, `game_saved_in_between`, tests unitaires de `diffOutside`).

### F3 — Copie interrompue du backup de départ : paire mélangée proposée

- **Emplacement** : `source/pp_ui.cpp:1022`.
- **Scénario** : « Restore start-of-session backup » copie d'abord `session_*` sur `backup_*`. Si `backup_personal.dat` est copié et que l'ouverture de `backup_main.dat` échoue, l'UI affiche une erreur mais laisse `backupTrusted` à vrai. La paire `backup_*` contient alors `personal` de l'état A et `main` de l'état B. Chaque fichier est valide seul, donc « Restore SD backup » l'accepte et écrit le mélange en NAND.
- **Gravité réelle** : le mélange reste un état que l'app aurait pu produire (poches d'origine, prêt modifié), pas une corruption. Mais l'UI affirme restaurer « l'état d'avant le dernier Apply », ce qui est faux, et l'invariant I6 est violé.
- **Preuve** : scénario `pre_edit_copy_fails` (fichier `backup_main.dat` en lecture seule). Code d'origine : le dialogue de restauration s'ouvre. 2 vérifications échouent. Même preuve côté UI dans `ui_harness.cpp` (« the copy over backup_*.dat fails »).
- **Correction** (`source/pp_ui.cpp:1049-1056` après correction) : `backupTrusted = false` dès que la copie ou la vérification qui suit échoue, `true` quand elles réussissent. Le message précise que le backup courant est incomplet et qu'un Apply en refait un.
- **Statut** : **corrigé + tests**.

## 4. Constats S1 à S3

### F4 (S1) — Écran noir de plusieurs minutes au lancement après une modification

- **Emplacement** : moteur, `source/main.cpp:119-127` (`GetLength`) et `:353-363` (`findRegions`). **Zone intouchable.**
- **Mécanisme** : le moteur ne recalcule jamais les hash. Après un Apply, la région éditée a donc un hash faux jusqu'à ce que le jeu resauvegarde. Au lancement suivant, `findRegions` ne reconnaît plus cette région, avance par pas de `0x10`, et rejoue à chaque pas un Murmur3 sur **tout le reste du fichier**. Rien n'est affiché pendant ce temps : la première image n'était dessinée qu'après le chargement.
- **Mesure** (`PP_NOSAN=1 sh tests/host/engine_bench.sh slow_scan`, Apple M4 Pro, save synthétique) :

  | État de la save | Durée du lancement |
  |---|---|
  | Tous les hash valides | 0,2 s |
  | Poches, Wallet ou Bank modifiés | 0,9 s |
  | Nook Miles modifiés | 2,0 s |
  | Prêt modifié | 13,1 s |

  Un Cortex-A57 à 1,02 GHz est de l'ordre de 15 à 25 fois plus lent sur ce code. **Estimation console, non mesurée** : 15 à 25 s après une modification de poches, environ 40 s après les Miles, **3 à 5 minutes après une modification du prêt**. La durée dépend aussi du hasard : le balayage s'arrête plus tôt quand il tombe sur une fausse région (voir E3).
- **Conséquence** : c'est le moment où l'utilisateur relance l'app parce que le jeu a refusé la save. Il voit un écran noir, croit à un plantage et ferme l'app. Rien n'est perdu, mais le filet de sécurité semble hors service.
- **Parade côté UI** (`source/main.cpp:352` après correction, `Ui::splash`) : une image « Reading the save… » est affichée avant le chargement, avec la mention que cela peut durer plusieurs minutes après une modification. Le README le dit aussi (Known limitations, Troubleshooting).
- **Statut** : **atténué**. La cause est dans le moteur. Une vraie correction (borner le balayage, ou utiliser la table fixe `REV_300`) relève de la décision D1 de l'audit. **Non vérifié sur console** : que l'image présentée avant la boucle principale s'affiche bien.

### F5 (S1) — Restauration et Doctor refusés quand la carte SD est pleine

- **Emplacement** : `source/pp_ui.cpp:1025-1028` et `:1043`.
- **Scénario** : la restauration et le Doctor exigeaient d'abord une copie de l'état courant dans `session_*.dat` (10,7 Mo). Si la carte n'a plus de place, le sauvetage était refusé, alors que le backup à restaurer est déjà sur la carte. Apply, lui, proposait « Apply anyway ».
- **Preuve** : scénario `restore_when_sd_full`. Code d'origine : pas de dialogue, rien de restauré. 6 vérifications échouent.
- **Correction** (`source/pp_ui.cpp:1028-1037` après correction, `afterPreEditBackup`) : même dialogue que pour Apply, avec « Restore anyway » ou « Repair anyway » et un texte qui dit que l'état courant ne sera conservé nulle part. Bouton par défaut : Cancel.
- **Statut** : **corrigé + test**. Avec F1, ce cas devient rare : dans le scénario de sauvetage normal, le backup pre-edit existe déjà et rien n'est à écrire.

### F6 (S1) — A maintenu au lancement : l'erreur fatale se ferme sans être lue

- **Emplacement** : `source/pp_ui.cpp:1575`, `source/main.cpp:673-677`.
- **Mécanisme** : au premier `padUpdate`, libnx rapporte comme « vient d'être pressé » tout bouton déjà enfoncé. Sur l'écran « The save can't be opened » (atteint très vite quand le jeu tourne encore), A quitte l'app.
- **Preuve** : `launch_guards` (A maintenu dès la première image : l'app quitte à l'image 1) et `ui_harness.cpp`.
- **Correction** (`source/pp_ui.cpp:1706-1713` après correction) : A, B, X, Y, L, R, + et − sont ignorés jusqu'à ce qu'ils aient tous été relâchés une fois. Les directions ne sont pas concernées, pour qu'un stick qui dérive ne bloque pas l'app.
- **Statut** : **corrigé + tests**.

### F7 (S1) — `font.ttf` illisible : interface vide, boutons actifs

- **Emplacement** : `source/main.cpp:303` (retour de `ui.init` ignoré), `source/pp_ui.cpp:1645`.
- **Scénario** : la garde ne teste que l'existence du fichier. Avec une police tronquée ou corrompue, aucun texte n'est dessiné, mais les boutons fonctionnent : on peut modifier et appliquer à l'aveugle.
- **Preuve** : `launch_guards` (police illisible : le code d'origine monte la save et fait tourner l'UI).
- **Correction** (`source/main.cpp:303-322` après correction) : si `ui.init` échoue, l'app ferme SDL, affiche un message console et quitte, **avant** d'ouvrir la save.
- **Statut** : **corrigé + test**. **Non vérifié sur console** : `consoleInit` appelé après `SDL_Quit`. Ce chemin n'est pris que dans un cas déjà en panne.

### F8 (S2) — Récap : le focus retombe sur Apply après « Revert this change »

- **Emplacement** : `source/pp_ui.cpp:1167-1170` et `:1093`.
- **Scénario** : `+`, ↑, A, A annule une ligne. `buildReviewRows` remet le focus sur le premier bouton, **Apply**. Un troisième A, par réflexe, écrit toutes les autres modifications en NAND.
- **Preuve** : `review_revert_focus`. Code d'origine : 2 écritures NAND, 1 commit, toast « Changes applied ».
- **Correction** (`source/pp_ui.cpp:1191-1207` après correction) : le focus reste dans la liste, sur la ligne voisine. S'il ne reste aucune ligne, il va sur le bouton sûr (Back). `m.top` est remis à zéro à chaque reconstruction.
- **Statut** : **corrigé + test**.

### F9 (S2) — Un loadout de 160 octets au contenu invalide est chargé tel quel

- **Emplacement** : `source/pp_model.cpp:472-481`, `source/pp_ui.cpp:848-849`.
- **Scénario** : `l_N.dat` de la bonne taille mais au contenu quelconque (fichier d'un autre outil, copie abîmée). Les 160 octets vont dans les poches, y compris des ID `0xFFFD` (case fantôme) ou `0xFFFF`. Le Récap les montre, mais comme « ID FFFD ».
- **Correction** : `pocketsPlausible` refuse tout enregistrement dont l'ID n'est ni vide ni dans `0000…FFFC`. `loadoutRead` l'applique. Le panneau affiche « Damaged file » et désactive « Load ».
- **Statut** : **corrigé + tests** (unitaires et fuzz).

### F10 (S2) — Sans `items.txt`, plus de protection « 1/slot »

- **Emplacement** : `source/pp_model.cpp:244`, `:251`, `:288`.
- **Mécanisme** : les règles « 1/slot » sont résolues par nom ou par ID borné à la taille d'`items.txt`. Si le fichier manque, `isSingle` répond toujours faux : un outil posé par ID reçoit une quantité brute non nulle au lieu de 0.
- **Statut** : **texte corrigé** (la bannière dit « no item names (IDs shown) and no 1/slot protection », README mis à jour). **À décider** : interdire de poser un item tant qu'`items.txt` manque. Non fait, car la spec UX (§ 7) dit que l'édition reste possible en mode dégradé.

### F11 (S2) — Stepper de quantité : valider sans bouger peut créer une modification

- **Emplacement** : `source/pp_ui.cpp:533-538`.
- **Scénario** : un slot dont la valeur brute est hors de 1…99 (par exemple 0, ce que le jeu stocke probablement pour « 1 item ») s'ouvre sur une valeur bornée. Valider sans rien toucher écrivait cette valeur bornée.
- **Correction** : confirmer la valeur affichée à l'ouverture n'écrit rien.
- **Statut** : **corrigé**. Limite restante, liée à la décision D4 (`kCountZeroBased`) : tant que la convention n'est pas tranchée, un slot à valeur brute 0 s'affiche « ×0 ».

### F12 (S2) — Backup de départ raté : fichiers partiels laissés sur la carte

- **Emplacement** : `source/main.cpp:634-636`, `source/pp_model.cpp:508-515`.
- **Scénario** : carte presque pleine. `session_personal.dat` est écrit, `session_main.dat` s'arrête à mi-chemin. Les deux restent, occupent la place, et « Apply anyway » échoue à son tour sur le backup du moteur.
- **Preuve** : `sd_card_full` (fichiers encore présents après l'échec, sur le code d'origine).
- **Correction** : en cas d'échec, les deux fichiers sont supprimés.
- **Statut** : **corrigé + test**.

### F13 (S3) — Textes faux ou trompeurs

| Chaîne | Problème | Correction |
|---|---|---|
| `D_RESTORE_B2` (`pp_strings.h:208`) | « The current console state is copied to the start-of-session backup first » : faux dès que ce backup existait déjà dans la session | Dit que ce qui a été appliqué depuis est perdu et que le backup pre-edit reste sur la carte |
| `BK_CORRUPT` (`:220`) | « looks damaged » alors que le cas courant est « le jeu a sauvegardé depuis » | Dit les deux causes et que le backup ne peut pas être restauré |
| `E_SESSION_COPY` (`:222`) | « nothing was restored », affiché aussi pour le Doctor | Remplacé par le dialogue de F5 |
| `E_TAINTED` après un Doctor raté | « A repair failed earlier » au moment même de l'échec, sous le titre « Nothing was written » | Nouveau dialogue « Repair failed » |
| `NO_ITEMS_BANNER` (`:234`) | Ne disait pas que le « 1/slot » est désactivé | Voir F10 |
| `T_LO_BAD` | « (wrong size) » | « Loadout file is damaged » |
| Exemples `16A2` (README, clavier, `pp_model.h`) | Présenté comme « Nook Miles Ticket » : dans `items.txt`, `0x16A2` est « (DIY recipe) » ; le ticket est `0x16DB` | Remplacé par `16DB` |
| README, « How your save is protected » | Ne disait pas que les backups sont inutilisables hors de l'app ni après une sauvegarde du jeu | Ajouté |

**Statut** : **corrigé**. Les captures `docs/screenshots/system.png` et `danger-dialog.png` ont été régénérées par le banc UI.

### F14 à F17

- **F14 (S3)** — `source/main.cpp:563-564` : si `origP` ou `origM` ne peut pas être alloué, l'écran fatal affichait le texte upstream « Ready. ZL restores backup. (+) Search, (A) Save/Quit ». **Corrigé** : « Not enough memory to load the save. ».
- **F15 (S3)** — `source/main.cpp:568` : en lecture seule, `hooks.pockets` pointait à `personal + 0x37CA8` même si le fichier est plus court. **Corrigé** : écran fatal « Unexpected save file size. » si les poches ne tiennent pas dans le fichier. Le chargement du moteur, lui, lit quand même hors tampon dans ce cas (E6).
- **F16 (S3)** — recherche annulée (`pp_ui.cpp:720-727`), fenêtre du Récap non recadrée après reconstruction (`:1093`), loadout endommagé affiché « Saved · 0/20 items » (`:812-818`). **Corrigé.**
- **F17 (S3)** — `source/pp_ui.cpp:547-554` : « Enter item ID (hex) » accepte tout ID de `0000` à `FFFC`, même absent d'`items.txt`. Le Récap l'affiche « ID 3FFF ». **Non corrigé** : c'est une fonction experte voulue par la spec. **À décider** : un dialogue de confirmation pour un ID inconnu.

## 5. Défauts du moteur hérité (documentés, non modifiés)

| # | Emplacement | Défaut | Conséquence | Parade côté UI |
|---|---|---|---|---|
| E1 | `main.cpp:448`, `:493`, `:550` | Le retour de `fsFsCommit` est ignoré | Un commit échoué s'affiche « Changes applied ». Le banc le reproduit (`commit_fails`) : la save simulée est inchangée et l'UI dit le contraire | Aucune possible. Au lancement suivant, la signature ne correspond pas et un backup neuf est pris. Décision D2 de l'audit |
| E2 | `main.cpp:114-118` | Les hash Murmur3 ne sont jamais recalculés | Après une modification, 1 hash faux par région touchée (le banc mesure 18/19 sur `main.dat` après un changement de prêt). Tolérance du jeu non prouvée | Aucune. Décision D1. **C'est le premier risque pour la save** |
| E3 | `main.cpp:353-363` | Balayage quadratique, et faux positifs | Lenteur (F4). Environ 2 % de chances par lancement de « trouver » une région qui n'existe pas (8,5 × 10⁷ comparaisons sur 32 bits, save saine) | Les régions ne servent plus qu'au diagnostic de System › Info. La validation des backups ne s'appuie pas dessus |
| E4 | `main.cpp:186-192` | Ni le code retour ni le nombre d'octets lus ne sont vérifiés | Une lecture courte laisserait des octets non initialisés, réécrits au prochain Apply | Partielle : contrôle des tailles (I9) |
| E5 | `backupToSd`, `doRestore` | Les backups ne contiennent pas `personalHeader.dat` ni `mainHeader.dat` (la clé) | Inutilisables avec un autre outil, et morts dès que le jeu régénère sa clé | La validation (F2) refuse un backup qui ne correspond plus. README mis à jour. **JKSV ou Checkpoint reste le seul vrai backup** |
| E6 | `main.cpp:404-407`, `:98-100` | Lectures aux offsets fixes sans contrôle de taille | Lecture hors tampon sur une save très courte (aucune version réelle d'ACNH n'est dans ce cas) | F15 pour la partie UI |
| E7 | `main.cpp:543` | Le Doctor remplace `origP` / `origM` avant d'écrire | Après un échec, `origP` décrit un état jamais écrit | Drapeau `tainted` (I7), vérifié par `save_doctor` |
| E8 | `main.cpp:476` | La restauration ne contrôle que la taille | Écrirait n'importe quel fichier de la bonne taille | I8 renforcé (F2) |
| E9 | `main.cpp:198-204` | Le backup est supprimé avant d'être recréé | Un échec laisse un `backup_main.dat` de la bonne taille, rempli de zéros (reproduit par `backup_main_left_empty`) | I6 en mémoire, plus I8 qui tient après relance |

Le Save Doctor, exécuté sans modification en attente, réécrit exactement les mêmes octets (`save_doctor` : fichiers identiques avant et après). Il ne répare donc rien sur une save dont seul un hash est faux.

## 6. Vérification des invariants I1 à I13

| Invariant | Respecté ? | Preuve |
|---|---|---|
| I1 zone d'écriture | Oui | `apply_restore_cycle` : le fichier écrit est égal, octet pour octet, à l'attendu calculé hors moteur ; le compteur de slots qui suit les poches est intact |
| I2 synchronisation avant `doSave` | Oui | Test unitaire, et le slot 1 n'est jamais altéré dans le banc (`g_slot` vaut toujours 1) |
| I3 un seul appelant | Oui | Script du moteur ; compteurs de commits du banc (1 par Apply, jamais 0 ni 2) |
| I4 Base reprise après succès seulement | Oui | `nand_write_fails`, `ui_harness` (Discard après un échec rend l'état console) |
| I5 zéro modification en attente | Oui | `ui_harness`, relecture de `blockedReason` |
| I6 backup non fiable | **Non → corrigé** | F3 |
| I7 session douteuse | Oui | `save_doctor` : ni restauration, ni Doctor, ni édition, ni Apply après l'échec |
| I8 restauration validée | **Trop faible → renforcé** | F2 |
| I9 version supportée | Oui | `read_only_save` : 0 écriture NAND, 0 création SD, aucune mutation par aucun chemin de l'UI |
| I10 valeurs bornées | Oui, **sauf loadouts → corrigé** | F9 |
| I11 argent lu dans `g_vals` | Oui | Relecture : l'UI ne lit aucun octet d'argent dans les tampons |
| I12 loadout par tampon temporaire | Oui | Tests unitaires et fuzz |
| I13 aucun nouvel appel d'écriture | Oui | Script du moteur (nombre d'appels identique à l'upstream) |

## 7. Ce qui n'a pas pu être vérifié sans console

1. **Le jeu accepte-t-il une save dont un hash est faux ?** C'est la question h de l'audit. Aucun banc ne peut y répondre.
2. **La convention de quantité** (`kCountZeroBased`) : inchangée, à trancher par le test du README.
3. **La journalisation de la savedata** : le banc la suppose (« pas de commit, rien de modifié »). Tous les messages « The console save was not changed » en dépendent.
4. **Le jeu régénère-t-il sa clé à chaque sauvegarde ?** NHSE dérive l'en-tête d'une graine fournie par l'appelant ; rien ne dit ce que fait le jeu. Le correctif F1 n'en dépend pas, et le correctif F2 est correct dans les deux cas.
5. **Les durées de F4** : estimées par règle de trois depuis un Mac.
6. **libnx et newlib** : `fopen` / `fwrite` de 10 Mo sur `sdmc:` en FAT32 et exFAT, `remove` puis `rename`, deux sessions SD ouvertes en même temps (stdio et `fsOpenSdCardFileSystem`). Le banc utilise le système de fichiers du Mac.
7. **Mémoire réelle** : pic d'environ 32 Mo pendant Apply (deux tampons de 10 Mo en permanence, plus la copie de travail de `doSave`), et 10 Mo de plus pendant la validation d'un backup, jamais en même temps.
8. **Rendu** : SDL sur Switch, VSync, 150 textures recréées à chaque rafraîchissement, glyphes de la police. Le banc UI tourne avec le SDL du Mac.
9. **Deux chemins nouveaux** : l'image d'attente présentée avant la boucle principale (F4), et `consoleInit` après `SDL_Quit` (F7).
10. **Compilation devkitA64** : seule la vérification de syntaxe avec clang et les stubs a tourné ici. GCC peut émettre des avertissements que clang n'émet pas. La CI le dira au prochain push.
11. **L'étape CI « Engine bench »** : écrite et lancée sur macOS seulement. Elle est en `continue-on-error` jusqu'à un premier passage vert sous Linux. Le scénario `pre_edit_copy_fails` se saute tout seul s'il tourne en root.

## 8. Risques résiduels pour la save, par probabilité

| Probabilité | Risque | Ce qui protège |
|---|---|---|
| **Élevée, inconnue** | Le jeu refuse ou « répare » une save dont un hash est faux (E2) | Backup JKSV ou Checkpoint avant le premier essai. Le backup pre-edit de l'app, restaurable tant que le jeu n'a pas resauvegardé (F1) |
| **Élevée** | Quantités décalées de 1, ou variante n° 1 d'un meuble non listé dans `nonstackable.txt`, tant que D4 n'est pas tranchée | Le test du README. Pas de corruption attendue, mais des items différents de ceux demandés |
| Moyenne | L'utilisateur ferme l'app pendant le balayage lent en croyant à un plantage, et ne restaure pas (F4) | Écran d'attente, README |
| Moyenne | Les backups de l'app deviennent inutilisables dès que le jeu sauvegarde, et le sont toujours hors de l'app (E5) | README. Backup externe |
| Faible | Commit échoué affiché comme un succès (E1) | Rien côté UI. La save reste l'ancienne (hypothèse de journalisation) |
| Faible | ID invalide saisi à la main, ou `items.txt` absent (F10, F17) | Le Récap affiche l'ID |
| Faible | Lecture courte de la save au chargement (E4), réécrite par Apply | Contrôle des tailles seulement |
| Très faible | Coupure de courant pendant l'écriture | Journalisation (non vérifiée) |
| Très faible | Collision de la signature 64 bits (F1) : un backup pre-edit serait conservé à tort | La validation F2 le refuserait à la restauration |

## 9. À décider par l'utilisateur

1. **F1** — valider le nouveau sens du backup `session_*.dat` (pre-edit, conservé entre les lancements).
2. **F2** — valider qu'un backup antérieur à la dernière sauvegarde du jeu soit refusé.
3. **F5** — valider « Restore anyway » et « Repair anyway » quand la copie préalable échoue.
4. **F10** — bloquer la pose d'items quand `items.txt` manque ?
5. **F17** — confirmer avant de poser un ID absent d'`items.txt` ?
6. **D1 et D2 de l'audit** (recalcul des hash, contrôle du commit) : le banc `engine_bench.sh` permet maintenant de tester une modification du moteur octet par octet avant de la mettre sur console. D1 supprimerait aussi F4.
7. **CI** — retirer `continue-on-error` de l'étape « Engine bench » une fois vue verte.

## 10. Relancer les vérifications

```sh
python3 tools/check_engine_untouched.py upstream/main
sh tests/host/run.sh
sh tests/host/engine_bench.sh                 # ou un nom de scénario en argument
sh tests/host/ui_shots.sh <dossier de sortie>
PP_NOSAN=1 sh tests/host/engine_bench.sh slow_scan    # mesure de F4, hors série par défaut
```

Pour reproduire les preuves sur le code d'origine :

```sh
mkdir -p /tmp/src_f993e45
for f in main.cpp pp_model.cpp pp_model.h pp_strings.h pp_ui.cpp pp_ui.h stb_image.h; do git show f993e45:source/$f > /tmp/src_f993e45/$f; done
PP_SRC=/tmp/src_f993e45 sh tests/host/engine_bench.sh     # 23 échecs attendus
```

Fichiers modifiés : `source/main.cpp` (zone hooks uniquement), `source/pp_model.*`, `source/pp_ui.*`, `source/pp_strings.h`, `tests/host/test_model.cpp`, `tests/host/ui_harness.cpp`, `README.md`, `.github/workflows/build.yml`, deux captures dans `docs/screenshots/`.
Fichiers ajoutés : `tests/host/engine_bench.sh`, `tests/host/engine/` (3 fichiers), ce rapport.
