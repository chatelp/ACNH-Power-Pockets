# Audit du chemin d'écriture de la sauvegarde — ACNH Power Pockets

> **Portée** : audit en **lecture seule** de `source/main.cpp` (upstream cbreezy210/ACNH-Save-Editor, commit `731c228`, 990 lignes). Les références `l.NNN` renvoient à ce fichier. Aucun code n'a été modifié.
> **Question posée** : le moteur d'écriture peut-il supporter le modèle « BIOS » de la refonte (modifs préparées en mémoire, puis **Appliquer** = un seul `doSave()`, ou **Annuler** = abandon en RAM), sans modification du moteur ?
> **Documents liés** : `docs/ux/UX_SPEC.md` (spec UX, session `pp-ux-menus`) — ses flux sont audités au § 5. Références externes : code source de NHSE (kwsch/NHSE, branche `master`), voir § 7.

---

## 0. Résumé exécutif

**Verdict global : le modèle BIOS est supporté par le moteur tel quel, avec des adaptations côté UI uniquement.** Aucune modification du moteur n'est *nécessaire* pour préparer plusieurs modifs, appliquer en un seul `doSave()`, annuler sans écriture, ou calculer un récapitulatif avant → après.

| Point | Verdict |
|---|---|
| a. Plusieurs slots, un seul `doSave()` | ✅ OK tel quel |
| b. Argent + poches ensemble, un seul `doSave()` | ✅ OK tel quel |
| c. Écriture de `g_newId`/`g_newCount` dans `g_slot` | 🟡 OK avec adaptation UI (synchronisation obligatoire juste avant l'appel) |
| d. Annuler toutes les modifs sans NAND | ✅ OK avec l'existant (deux méthodes) |
| e. Diff avant → après | 🟡 OK avec adaptation UI (instantané « Base ») |
| f. Échecs partiels | 🟡 OK avec gardes UI — **un risque critique** sur le backup SD (§ 3.f) |
| g. Restauration ZL et Doctor avec modifs en attente | 🟡 OK avec gardes UI (zéro modif en attente, plus 3 gardes supplémentaires) |
| h. Hash Murmur3 | 🔴 **Aucun recalcul nulle part** : à confirmer avec l'utilisateur (§ 3.h) |
| i. Format des enregistrements de poche | 🔴 **Sémantique de la quantité probablement « quantité − 1 »**, octets hérités risqués : à confirmer (§ 3.i) |

**Trois alertes à confirmer avec l'utilisateur avant d'implémenter :**

1. **Hash d'intégrité jamais recalculés.** `Murmur3Hash` (l.109) n'est appelé nulle part. L'étape « Re-heal all detected regions » du Doctor (l.793) est vide, et le README (l.12, l.59) annonce une fonction qui n'existe pas dans le code. Toute modif des poches, du Wallet ou de la Bank invalide le hash de la région 2 de `personal.dat`. Que le jeu l'accepte ou non n'est pas établi.
2. **La valeur stockée à +4 semble être « quantité − 1 »** (convention suivie par l'éditeur d'items de NHSE). La règle « quantité forcée à 1 » de la spec écrirait en réalité 2 items, ou la variante n° 1 d'un meuble, ou la recette n° 1 d'une fiche DIY.
3. **Un échec de backup SD à mi-parcours peut laisser un `backup_main.dat` de la bonne taille mais vide.** La restauration ZL, qui ne contrôle que la taille (l.728), l'écrirait alors en NAND. Une garde UI suffit à l'éviter (§ 3.f, § 4).

Par ailleurs, les offsets codés en dur correspondent à la **version de save 3.0.0 – 3.0.3** (classes `PersonalOffsets30` / `MainSaveOffsets30` de NHSE), et l'upstream **ne contrôle pas la version**. Garde UI recommandée : refuser l'édition si `pSz != 0x74A40` ou `mSz != 0x9B0E90` (§ 4).

---

## 1. Cartographie du chemin d'écriture

### 1.1 Chargement (l.509-591)

| Étape | Lignes | Détail |
|---|---|---|
| Montage de la savedata ACNH | l.509-511 | `fsOpenSaveDataFileSystem`, `application_id = 0x01006F8002326000`, `save_data_type = 3`. Le handle `fs` reste ouvert jusqu'à l.987. |
| Lecture des 4 fichiers | l.518-521 | `/Villager0/personal.dat`, `/Villager0/personalHeader.dat`, `/main.dat`, `/mainHeader.dat` via `readSaveFile`. |
| `readSaveFile` | l.185-191 | `fsFileGetSize` puis `malloc(sz)` puis `fsFileRead`. **Ni le code retour ni `rd` ne sont vérifiés** : une lecture courte laisserait des octets non initialisés dans le tampon, qui seraient réécrits plus tard. Pas de garde possible côté UI, sauf le contrôle de taille et le diagnostic de hash (§ 4). |
| Copies d'origine | l.522-525 | `origP = copie(personal)`, `origM = copie(mainData)` **avant déchiffrement** : `origP`/`origM` = octets **chiffrés**, tels qu'en NAND. |
| Déchiffrement | l.531 | `CryptACNH(pHdr, personal, pSz)` et `CryptACNH(mHdr, mainData, mSz)`, en place. `personal`/`mainData` sont désormais **en clair**. |
| Régions Murmur3 | l.533-543 | `findRegions` : à partir de 0x100, pas de 0x10, cherche une position où `LE32(buf+off)` est égal au Murmur3 des `len` octets suivants (`GetLength`, l.114-122). **Lecture seule.** |
| Cache de layout | l.546-581 | Garde la liste de régions la plus longue entre le scan et `layout_*.dat` sur la SD, sans jamais écraser un cache plus long. **Lecture seule vis-à-vis de la save** (écrit seulement sur la SD). |
| Décodage des valeurs | l.584-587 | `g_vals[i] = DecInt(LE32(p+off), p[off+6], LE16(p+off+4))` pour Wallet, Bank, Miles (personal) et Loan (main). |
| Slot courant | l.589-590 | `g_newId`/`g_newCount` = contenu du slot 1. |
| Garde d'entrée | l.686 | Toute l'interaction est désactivée si un des 6 tampons est nul. |

### 1.2 Primitives

- **`CryptACNH` (l.92-104) — symétrique : confirmé.** C'est de l'AES-128 en mode CTR. La clé et le compteur initial sont dérivés à **chaque appel** de `header + 0x100` (`GetParam`, l.95). Les blocs du compteur sont chiffrés avec `AesEncryptBlock` (seule la fonction de chiffrement AES est utilisée), puis le flux est XORé sur les données (l.101). Le compteur est une variable locale réinitialisée à chaque appel. Appliquer `CryptACNH` deux fois avec le même en-tête redonne donc les octets d'origine. Les en-têtes `pHdr`/`mHdr` ne sont **jamais modifiés ni réécrits** dans tout le programme. Le chiffrement est donc reproductible pendant toute la session.
- **`DecInt` / `EncInt` (l.131-132)**, **`CalcChecksum` (l.128)** : identiques à `EncryptedInt32` de NHSE (constante `0x80E32B11`, rotation `shift + 3`, `adjust`, checksum = somme des octets − 0x2D). Disposition sur disque : u32 chiffré à +0, `adjust` u16 à +4, `shift` u8 à +6, checksum u8 à +7.
- **`applyEdit` (l.177-183)** : relit `adjust`/`shift` **dans le tampon cible**, réencode, écrit +0..+3 et +7. Il ne touche pas +4..+6. **Déterministe** : réencoder la valeur déjà présente redonne exactement les mêmes 8 octets, si le champ d'origine était valide. Si le checksum d'origine était faux, `applyEdit` le corrige.
- **`backupToSd` (l.194-209)** : ouvre la SD et, pour chacun des deux fichiers : `fsFsDeleteFile`, `fsFsCreateFile(taille)`, `fsFileWrite(Flush)`. Les deux appels sont chaînés par `&&` : si le premier échoue, le second n'est pas tenté. Arguments passés par `doSave` : `origP`/`origM` (l.621), donc les **octets chiffrés de l'état NAND validé** (chargement, ou dernière écriture réussie). Le backup est donc toujours **l'état d'avant l'écriture en cours**. C'est un **backup unique, écrasé à chaque Appliquer** : après deux Appliquer dans la même session, l'état du lancement est perdu.

### 1.3 `doSave` (l.620-663), pas à pas

| # | Lignes | Action | Effet en cas d'échec |
|---|---|---|---|
| 1 | l.621 | `backupToSd(origP, pSz, origM, mSz)` | Échec : `g_status_buf = bakMsg` (« BAK… »), **sortie, rien d'écrit en NAND**. |
| 2 | l.623-625 | `malloc(pSz)` et `malloc(mSz)` (≈ 0,47 Mo + 10,2 Mo) | Échec : « malloc failed! » (l.659), rien d'écrit en NAND. Le backup a déjà été réécrit, mais avec un contenu identique (`origP`). |
| 3 | l.626 | `outP = copie(personal)`, `outM = copie(mainData)` (en clair) | — |
| 4 | l.627-630 | `applyEdit` ×4 : Wallet, Bank, Miles dans `outP`, Loan dans `outM` (**inconditionnel**) | — |
| 5 | l.635-638 | « Heal » : `WriteLE32(outM + 0x5B3D50 + i*0x74A40, 0)` pour i = 0..7 (**inconditionnel**) | — |
| 6 | l.640-641 | `WriteLE16(slot g_slot +0, g_newId)`, `WriteLE16(+4, g_newCount)` dans `outP` | — |
| 7 | l.643 | `CryptACNH` en place sur `outP` et `outM` | — |
| 8 | l.645-651 | `writeNand(personal.dat)` **puis**, si succès, `writeNand(main.dat)` (ouverture en écriture, `fsFileWrite` avec Flush depuis l'offset 0, taille inchangée) | Échec : « NAND write failed! » (l.658), **pas de commit**. |
| 9 | l.652 | `fsFsCommit(&fs)` — **code retour ignoré** | Un échec du commit n'est pas détecté. |
| 10 | l.653-654 | `origP = outP` (chiffré), `personal = déchiffre(outP)` ; idem pour main | Exécuté seulement si l'étape 8 réussit. |
| 11 | l.655-656 | Réécrit `g_newId`/`g_newCount` dans `personal` au slot `g_slot` (redondant : ces octets y sont déjà depuis l'étape 6) | — |
| 12 | l.657 | `g_status_buf = "SAVED personal + main to NAND!"` | — |
| 13 | l.660-661 | `free(outP)`, `free(outM)` | — |

Propriété essentielle pour le modèle BIOS : **`doSave` ne lit l'état d'édition qu'à travers `personal` (copié en entier), `mainData` (copié en entier), `g_vals[4]`, `g_slot`, `g_newId` et `g_newCount`. Il ne modifie `personal`, `mainData`, `origP` et `origM` qu'en cas de succès des deux écritures NAND.**

### 1.4 Autres chemins d'écriture

- **« Vider le slot » (−, l.710-718)** : `g_newId = 0xFFFE`, `g_newCount = 0`, puis `WriteLE16(+0, 0xFFFE)` et `WriteLE16(+4, 0)` dans `personal` au slot `g_slot`. **RAM seulement.** +2, +3 et +6 ne sont pas touchés.
- **Chargement de loadout (B sur l'écran 3, l.892-910)** : `fread(personal + POCKETS_BASE, 1, 160, f)` **directement dans le tampon de travail**. Une lecture courte (< 160) écrase partiellement les poches avant que l'erreur « too small » soit affichée (l.904). RAM seulement. La spec UX corrige ce défaut avec un tampon temporaire (UX_SPEC § 5.8).
- **Sauvegarde de loadout (A sur l'écran 3, l.879-891)** : `fwrite(personal + POCKETS_BASE, 160)` vers `l_N.dat` sur la SD. Ne touche pas la save.
- **Restauration ZL (l.720-770)** :
  1. lit `backup_*.dat` et contrôle **uniquement leur taille** (l.728) ;
  2. lit les deux fichiers dans des tampons `malloc` ;
  3. `writeNand2(personal)` puis `writeNand2(main)` ;
  4. si les deux réussissent : `fsFsCommit` (retour ignoré), `origP`/`origM` = backup, `personal`/`mainData` = backup déchiffré, `g_vals` redécodées, `g_newId`/`g_newCount` relus au slot `g_slot` (l.745-757).

  L'état NAND remplacé **n'est sauvegardé nulle part**.
- **Save Doctor (ZR deux fois, l.773-815)** :
  1. copie « médico-légale » de `origP`/`origM` vers `damaged_*.dat` (l.780-792). C'est l'**état validé courant**, pas la RAM ;
  2. étape « re-heal » **vide** (l.793-794) ;
  3. `applyEdit` ×4 **dans `personal`/`mainData` eux-mêmes** (l.796-799) ;
  4. chiffrement en place (l.801) ;
  5. **`origP`/`origM` = tampons chiffrés, *avant* l'écriture** (l.802) ;
  6. `writeNandR` ×2, puis commit si succès (l.808-809) ;
  7. déchiffrement en place (l.810), quel que soit le résultat.

  Le Doctor **n'appelle pas `backupToSd`**, n'applique **pas** le « heal » des 8 champs et n'écrit **pas** `g_newId`/`g_newCount`. En revanche, il écrit **tout le tampon `personal` courant**, donc toute modif de poche en attente.

---

## 2. Tableau de synthèse des verdicts

| # | Question | Verdict | Lignes clés |
|---|---|---|---|
| a | N slots modifiés en RAM puis un `doSave()` | OK tel quel | l.626, l.640-641 |
| b | Argent + poches puis un `doSave()` | OK tel quel | l.626-630 |
| c | Écriture supplémentaire dans `g_slot` | OK avec adaptation UI | l.640-641, l.655-656 |
| d | Annuler sans NAND | OK avec l'existant | l.522-524, l.531, l.584-587, l.746-757 |
| e | Diff avant → après | OK avec adaptation UI | l.584-590 |
| f | Échecs partiels | OK avec gardes UI ; risque critique sur le backup | l.194-209, l.621-662 |
| g | ZL / Doctor avec modifs en attente | OK avec gardes UI | l.720-815 |
| h | Hash Murmur3 recalculés ? | **Non** — décision utilisateur | l.109, l.533-581, l.793 |
| i | Format des enregistrements de poche | Risques — décision utilisateur | l.640-641, l.711-715, l.837 |

---

## 3. Vérification point par point

### a. Plusieurs slots modifiés en mémoire, puis UN seul `doSave()` — ✅ OK tel quel

`doSave` copie **l'intégralité** de `personal` dans `outP` (l.626) avant de chiffrer et d'écrire. Toute écriture faite par l'UI dans `personal + POCKETS_BASE + (s-1)*8` pour s = 1..20 est donc écrite en NAND au prochain `doSave`, quel que soit le nombre de slots. C'est le même mécanisme que celui dont dépendent déjà « vider le slot » (l.713-715) et le chargement de loadout (l.896) dans l'upstream.

Seule réserve : voir le point **c**, l'écriture supplémentaire au slot `g_slot`.

**Invariant associé** : l'UI n'écrit dans `personal` **que** dans la plage `[POCKETS_BASE, POCKETS_BASE + 160)` = `[0x37CA8, 0x37D48)`. Tout octet modifié ailleurs dans `personal` serait écrit en NAND sans apparaître dans le récap. Elle n'écrit **jamais** dans `mainData`, `origP`, `origM`, `pHdr` ni `mHdr`.

### b. Wallet/Bank/Miles/Loan + poches ensemble, puis un seul `doSave()` — ✅ OK tel quel

- Les valeurs d'argent ne vivent **que** dans `g_vals[4]` jusqu'à l'écriture. `doSave` les encode dans les **copies** (l.627-630), pas dans `personal`/`mainData`. Les octets d'argent de `personal` restent ceux du chargement jusqu'au succès de l'écriture (l.653-654).
- Les 4 `applyEdit` sont **inconditionnels**. Comme `EncInt` est déterministe (§ 1.2), une valeur non modifiée redonne les mêmes octets : pas d'effet de bord, sauf correction d'un checksum déjà faux.
- Poches et argent partent dans la **même** paire d'écritures et le **même** commit (l.651-652).

Remarques :
- **`main.dat` est réécrit à chaque Appliquer**, même si seules les poches changent : réencodage du Loan et « heal » des 8 champs (l.635-638). On ne peut pas l'éviter sans toucher au moteur.
- **Invariant UI** : ne jamais lire l'argent dans les octets de `personal`/`mainData` pendant l'édition, mais toujours dans `g_vals`. Les octets ne sont à jour qu'après un succès.
- **Bornes** : `g_vals` est réencodé tel quel. L'UI doit garantir des valeurs bornées (upstream l.825-829). Le plafond in-game du Wallet (99 999) n'est pas imposé par l'upstream (999 999 999) : voir la décision D6.

### c. Écriture supplémentaire de `g_newId`/`g_newCount` dans `g_slot` — 🟡 OK avec adaptation UI

`doSave` écrit **toujours** `(uint16)g_newId` à +0 et `(uint16)g_newCount` à +4 du slot `g_slot` dans `outP` (l.640-641). Après un succès, il refait la même écriture dans `personal` (l.655-656). Dans le modèle BIOS, la vérité des poches est le tampon `personal`. Si ces trois variables sont périmées, le slot `g_slot` est **silencieusement écrasé**.

**Invariant à garantir immédiatement avant chaque appel à `doSave()`** (aucun code ne doit s'exécuter entre la synchronisation et l'appel) :

```
1 ≤ g_slot ≤ 20
(uint16)g_newId    == LE16(personal + POCKETS_BASE + (g_slot-1)*8)
(uint16)g_newCount == LE16(personal + POCKETS_BASE + (g_slot-1)*8 + 4)
```

Avec cet invariant, l.641 et l.656 réécrivent des valeurs identiques : **no-op**. La fonction `syncSaveSlotVars()` proposée par la spec (UX_SPEC § 6.3) le garantit exactement, bornage de `g_slot` compris. Elle est **validée par cet audit**.

Pourquoi le bornage est indispensable : avec `g_slot = 0`, l.640 écrirait en `0x37CA0`, qui se trouve dans la zone méta du **sac** (slots 21-40, assignations à la roue). Avec `g_slot = 21`, l'écriture tomberait en `0x37D48`, sur le **compteur de slots** de la poche (§ 3.i). L'upstream borne `g_slot` (l.832, l.866-872), mais la nouvelle UI ne doit pas dépendre de ce code d'input, qui sera réécrit.

Autres écritures de ces variables dans l'upstream : l.590, l.711-712, l.757, l.833, l.835, l.837, l.860-861, l.899-900, l.914-915. La nouvelle UI ne doit plus s'en servir comme état d'édition (cf. UX_SPEC § 6.3). Celle de l.757 (restauration) est sans danger, car `syncSaveSlotVars()` repasse avant tout `doSave()`.

### d. « Annuler toutes les modifs » sans écriture NAND — ✅ OK avec l'existant

Deux méthodes valides, n'utilisant que des fonctions existantes.

**Méthode 1 — instantané « Base » (recommandée, c'est celle de la spec, UX_SPEC § 4.5).** À la fin du chargement (après l.591) et après chaque écriture réussie (`doSave`, restauration, Doctor), copier `personal + POCKETS_BASE` (160 octets) dans `g_basePockets` et `g_vals` dans `g_baseVals`. Annuler = recopier ces deux zones. Coût nul, aucun déchiffrement. Elle est **exacte** tant que l'invariant du point a est respecté : l'UI n'écrit que dans ces deux zones.

**Méthode 2 — reconstruction complète depuis `origP`/`origM`** (filet de sécurité, ou « réinitialisation totale ») :

```
memcpy(personal, origP, pSz);  CryptACNH(pHdr, personal, pSz);
memcpy(mainData, origM, mSz);  CryptACNH(mHdr, mainData, mSz);
g_vals[0..3] = DecInt(...)     // mêmes 4 expressions que l.584-587
```

C'est **exactement** le bloc l.746-755 de la restauration ZL, sans `writeNand2` ni `fsFsCommit`, avec `origP`/`origM` à la place du backup. Elle repose sur la symétrie de `CryptACNH` (§ 1.2) et sur l'immuabilité de `pHdr`/`mHdr`. Coût : déchiffrement logiciel de ≈ 10,2 Mo pour `main.dat`, soit une fraction de seconde à quelques secondes, avec écran d'attente. Aucun `malloc`.

**Précondition commune** : `origP`/`origM` doivent refléter la NAND validée. C'est vrai après le chargement (l.523-524), après un `doSave` réussi (l.653-654) et après une restauration réussie (l.746-747). C'est **faux après un échec du Save Doctor** : l.802 met à jour `origP`/`origM` *avant* l'écriture, et ne les rétablit pas en cas d'échec. Cela peut aussi être faux après un commit échoué non détecté (point f). Dans ces deux cas, la méthode 2 reconstruirait un état qui n'est pas en NAND. → Garde `g_sessionTainted` (§ 4).

### e. Diff « avant → après » pour le récapitulatif — 🟡 OK avec adaptation UI

- **Poches** : comparer, pour s = 1..20, les 8 octets de `personal + POCKETS_BASE + (s-1)*8` avec `g_basePockets + (s-1)*8`. Afficher ID (+0) et quantité (+4) avant → après, et signaler si +2, +3 ou +6 diffèrent aussi (cas d'un loadout ou d'un rétablissement : « attributs modifiés »).
- **Valeurs** : `g_vals[i]` comparé à `g_baseVals[i]`.
- Calculer le diff directement depuis `origP` est **déconseillé** : `CryptACNH` ne sait déchiffrer qu'un tampon complet depuis l'offset 0, car le compteur CTR démarre à 0. Il faudrait déchiffrer une copie de 10 Mo pour lire 4 octets de Loan. L'instantané Base est l'équivalent exact et gratuit.
- **À ajouter au récap** (non visible dans le diff) : Appliquer réécrit aussi `main.dat` (réencodage du Loan et mise à zéro des 8 champs « heal », l.635-638). Le diagnostic `poisonFieldsReport` (§ 4) peut afficher si ces 8 champs valent déjà 0, auquel cas c'est un no-op.

### f. Échecs partiels — 🟡 OK avec gardes UI, **un risque critique**

Hypothèse sur la NAND : la savedata ACNH est une savedata **journalisée** d'Horizon. Les écritures ne deviennent définitives qu'au `fsFsCommit`, et une fermeture sans commit les annule. C'est la raison d'être de `fsFsCommit` dans libnx, mais **ce comportement n'a pas été vérifié sur console** : à confirmer.

| Cas | NAND (validée) | RAM (`personal`, `mainData`, `g_vals`) | `origP`/`origM` | Backup SD | Ce que l'UI doit faire |
|---|---|---|---|---|---|
| **SD non ouvrable** (« BAK sd open », l.196) | inchangée | inchangée, **modifs conservées** | inchangés | **intact** (non touché) | Erreur : « rien n'a été écrit ». Le backup reste fiable. |
| **Échec sur `backup_personal`** (« BAK write failed », l.207) | inchangée | modifs conservées | inchangés | `backup_personal` **supprimé ou vide** (supprimé l.198, puis créé à la bonne taille l.199 sans être rempli) ; `backup_main` = celui d'un Appliquer **précédent** → paire incohérente | Erreur + `g_backupTrusted = false` : **bloquer la restauration** jusqu'au prochain backup réussi. |
| **`backup_personal` OK, échec sur `backup_main`** (« BAK write failed ») | inchangée | modifs conservées | inchangés | `backup_personal` = bon ; **`backup_main` supprimé ou créé à la bonne taille mais vide** | ⚠ **Critique** : la restauration ZL ne contrôle que la taille (l.728). Si `fsFsCreateFile` a réussi et que l'écriture a échoué, ZL écrirait un `main.dat` vide en NAND. → `g_backupTrusted = false` **et** validation du contenu avant toute restauration (`validateBackupFiles`, § 4). |
| **`malloc` échoué** (l.659) | inchangée | modifs conservées | inchangés | réécrit à l'identique (= `origP`), fiable | Erreur « mémoire insuffisante, rien n'a été écrit ». |
| **Échec d'écriture `personal.dat`** (l.651, l.658) | inchangée (pas de commit) | modifs conservées | inchangés | fiable (état d'avant) | « La save n'a pas été modifiée. » Réessayer ou annuler. |
| **`personal.dat` écrit, échec `main.dat`** (l.651, l.658) | **inchangée** si journalisation (pas de commit) ; l'écriture de `personal.dat` reste **en attente dans le journal** de la session `fs` | modifs conservées | inchangés | fiable | Même message. L'écriture en attente est soit écrasée par une réécriture complète ultérieure (Appliquer, restauration et Doctor écrivent toujours les **deux** fichiers en entier), soit annulée à la fermeture (l.987, sans commit). **Ne jamais ajouter d'appel à `fsFsCommit` en dehors des chemins existants.** |
| **Commit échoué** (l.652, retour ignoré) | probablement inchangée | resynchronisée **comme si succès** | = état non validé | fiable (état d'avant) | **Indétectable sans modifier le moteur** : « SAVED » s'affiche quand même. Voir la décision D2. |
| **Coupure de courant pendant l'écriture** | protégée par la journalisation (à confirmer) | perdue | perdus | fiable | — |

**L'état « en attente » est-il conservé en cas d'échec ? Oui, dans tous les cas** : `doSave` ne travaille que sur des copies (l.623-626) et ne touche `personal`/`mainData` qu'après le succès des deux écritures (l.653-654). `g_vals` n'est jamais modifié par `doSave`. La Base ne doit être reprise **qu'après succès**, ce que la spec prévoit (UX_SPEC § 4.4).

**Détection du résultat** : `doSave` ne retourne rien. Le seul signal est `g_status_buf` : « SAVED » (l.657), « BAK sd open 0x… » (l.196), « BAK write failed! » (l.207), « NAND write failed! » (l.658), « malloc failed! » (l.659). La méthode de la spec (vider le tampon, appeler, tester le préfixe « SAVED », UX_SPEC § 6.4) est correcte, aux deux réserves près : le commit non vérifié, et le fait que « BAK… » ne distingue pas les trois sous-cas du tableau. L'UI doit donc traiter **tout** « BAK… » comme un backup non fiable.

### g. Restauration ZL et Save Doctor dans le nouveau modèle — 🟡 OK avec gardes UI

**Ce qui se passe s'il y a des modifs en attente :**

- **ZL** : `personal`/`mainData` sont remplacés par le backup (l.748-751), et `g_vals` est redécodé (l.752-755). **Les modifs en attente sont perdues sans avertissement.** L'écriture elle-même reste saine. En revanche, l'état NAND remplacé n'est sauvegardé nulle part : tout ce qui a été appliqué depuis le dernier backup disparaît.
- **Doctor** : il écrit en NAND **tout le tampon `personal` courant** (l.801, l.808), donc **les modifs de poches en attente**, ainsi que les **valeurs d'argent en attente** (`applyEdit(personal, …, g_vals[i])`, l.796-799). Tout cela se fait **sans `backupToSd`**, sans le « heal » et sans passer par le récap. C'est un « Appliquer caché sans backup ». La copie `damaged_*` contient `origP`/`origM`, c'est-à-dire l'état validé d'avant le Doctor. C'est donc de fait un backup, mais la restauration ZL ne sait pas le lire.
- **Doctor en échec** : `origP`/`origM` contiennent alors un état **jamais validé en NAND** (l.802). Au prochain Appliquer, `backupToSd(origP…)` écraserait le bon backup par cet état fictif.

**Gardes UI recommandées (toutes sans modification du moteur) :**

1. **Zéro modif en attente** pour lancer la restauration ou le Doctor. C'est la règle de la spec (UX_SPEC § 5.9-5.10) : **validée**. La garde est *impérative* pour le Doctor, et fortement recommandée pour la restauration (perte silencieuse du travail).
2. **`g_backupTrusted`** : passe à faux après tout « BAK… » dans la session. Tant qu'il est faux, la restauration est bloquée avec le message : « Le dernier backup n'a pas pu être écrit entièrement ; restaurer pourrait endommager la save. Appliquez de nouveau (le backup sera refait) ou utilisez un backup externe. »
3. **Validation du contenu du backup avant restauration** (`validateBackupFiles`, § 4) : lire les deux fichiers dans des tampons temporaires, **déchiffrer une copie** avec `CryptACNH(pHdr|mHdr, …)`, puis vérifier le checksum EncryptedInt32 des 4 champs connus (`buf[off+7] == CalcChecksum(LE32(buf+off))`). Un fichier vide ou corrompu échoue avec une probabilité ≈ 1 − 1/256 par champ. Optionnellement, ajouter le contrôle des hash de régions (point h). Lecture seule, aucune écriture.
4. **`g_sessionTainted`** : passe à vrai après un **échec du Doctor**. Tant qu'il est vrai, Appliquer, la restauration et l'annulation par la méthode 2 sont bloqués, avec le message : « Quittez complètement l'app et relancez-la avant toute autre action. » Au relancement, l'état validé est relu depuis la NAND.
5. **Après un succès** de la restauration ou du Doctor : reprendre la Base (`snapshotBase`). La spec le prévoit.
6. Rappeler dans le dialogue de restauration que l'état actuel de la console **n'est pas sauvegardé** (la spec le dit, § 5.9). Pour le conserver, voir la décision D5.

### h. Intégrité : les hash Murmur3 sont-ils recalculés ? — 🔴 **Non. Point à confirmer avec l'utilisateur.**

**Constat dans le code :**
- `Murmur3Hash` est défini (l.109-113) mais **n'est appelé nulle part** dans le fichier.
- `GetLength` (l.114-122) ne sert qu'à **trouver** les régions (`findRegions`, l.533-541).
- `pReg`/`mReg` ne servent qu'au cache de layout (l.564-581) et au message « DOCTOR armed (%d p-regs) » (l.776).
- L'étape « 2) Re-heal all detected regions » du Doctor (l.793-794) est **vide** (le commentaire est suivi directement de l'étape 4).
- Aucune écriture de hash dans `doSave`, ni dans la restauration, ni dans le Doctor.
- Le README affirme pourtant (l.12) « automatic hash healing » et (l.59) « The editor automatically recalculates and heals these hashes after every modification ». **C'est faux pour ce code.**

**Régions concernées** (table NHSE `FileHashRevision.REV_300`, save 3.0.x ; une région = hash u32 à `off`, données hashées `[off+4, off+4+len)`) :

| Fichier | Hash à | Données couvertes | Champs édités par l'app |
|---|---|---|---|
| personal.dat | 0x00110 | 0x00114 – 0x37BE0 | **Miles** (0x12718) |
| personal.dat | 0x37BE0 | 0x37BE4 – 0x74A40 (fin du fichier) | **Poches** (0x37CA8), **Wallet** (0x37D60), **Bank** (0x6527C) |
| main.dat | 0x1E34B0 | 0x1E34B4 – 0x5B3D50 | **Loan** (0x33CBF0) |
| main.dat | — | 0x5B3D50 + i×0x74A40 (les 8 champs « heal ») | **hors de toute région hashée** (voir plus bas) |

**Conséquence** : toute modification réelle d'une poche, du Wallet ou de la Bank laisse un hash faux à 0x37BE0. Une modif des Miles invalide le hash à 0x110, une modif du Loan celui à 0x1E34B0 de `main.dat`. Le modèle BIOS ne change rien à ce constat : il ne fait que regrouper les modifs.

**Indices que ce comportement a déjà été observé** : le cache de layout v1.4.3 (« pick the BEST layout, never pollute the cache », l.564-581) n'a de sens que si un scan retrouve **moins** de régions qu'avant. C'est exactement ce qui arrive quand un hash est devenu faux : `findRegions` ne reconnaît plus la région, la saute par pas de 0x10 et ne retrouve aucune région suivante. Effet secondaire probable : un **démarrage nettement plus lent**, car `GetLength` est rejoué sur tout le reste du fichier à chaque pas.

**Ce que l'on ne sait pas** : si le jeu **vérifie** ces hash au chargement (et s'il refuse la save, la restaure ou l'accepte). NHSE recalcule **systématiquement** tous les hash avant d'écrire (`EncryptedFilePair.Hash()`, appelé par `HorizonSave.Save()`), ce qui laisse penser que c'est nécessaire. Mais aucun texte de NHSE ne dit que le jeu les contrôle. L'upstream revendique « 1 000+ installs, zéro save perdue » (README l.12), ce qui laisse penser que le jeu ne refuse pas la save d'emblée. L'existence d'un « Save Doctor » et de champs « empoisonnés par une ancienne version buggée » (l.632-634) montre cependant qu'il y a déjà eu des incidents.

**Implications pour l'édition des poches** : si le jeu vérifie ces hash, *chaque* Appliquer produit une save que le jeu pourrait rejeter. Le modèle BIOS a au moins l'avantage de réduire le nombre d'écritures à une par session.

**Question à poser à l'utilisateur :** avez-vous déjà constaté qu'une save éditée par l'upstream (poches ou argent) se charge normalement en jeu, avec les modifs visibles ? Après une sauvegarde faite par le jeu, l'app retrouve-t-elle de nouveau toutes les régions (le jeu réécrit-il les hash) ?

**Diagnostic proposé (lecture seule, sans modifier le moteur)** : `regionHashReport`, qui recalcule `Murmur3Hash(buf+off+4, len)` et le compare à `LE32(buf+off)` pour chaque région de `pReg`/`mReg`, puis affiche « hash OK / KO » par région dans Système → Infos. Comme `pReg` peut provenir du cache, une région KO y apparaît. Protocole de test, sur une save **préalablement sauvegardée avec un outil externe** (JKSV, Checkpoint) :
1. lancer l'app sans rien modifier : relever le rapport ;
2. modifier un slot, appliquer, relancer l'app : la région 0x37BE0 devrait être KO ;
3. lancer le jeu : charge-t-il ? l'item est-il présent ? sauvegarder en jeu, quitter ;
4. relancer l'app : la région est-elle redevenue OK ?

Le résultat de ce test tranche la décision D1. **Rien n'est corrigé ici.**

**Note sur le « heal » des 8 champs** (l.635-638) : d'après la table de hash `REV_300` de main.dat, `0x5B3D50 + i×0x74A40` (i = 0..7) est le **début** de chacun des 8 blocs de 0x74A40 octets (= la taille de `personal.dat`) qui suivent la région 2 de `main.dat`. Chaque bloc a le même découpage de hash que `personal.dat` (hash à +0x110 et à +0x37BE0). Les 4 octets mis à zéro se trouvent donc dans l'en-tête **non hashé** du bloc (+0x000 à +0x10F). Ils ne cassent aucun hash. Leur signification n'est pas documentée dans NHSE (aucune propriété ne les nomme), et ils sont mis à zéro **à chaque Appliquer**, y compris sur une save saine. Diagnostic lecture seule proposé : `poisonFieldsReport`, qui affiche les 8 valeurs actuelles. Si elles valent déjà 0, l'opération est sans effet.

### i. Format des enregistrements de poche — 🔴 risques, décisions utilisateur

**Emplacement (NHSE `PersonalOffsets30`)** :
- `PlayerOther = 0x37BE0`.
- `Pockets1 = 0x37BF0` : **sac, slots 21-40**.
- `Pockets2 = Pockets1 + 8×20 + 0x18 = 0x37CA8` : **poche, slots 1-20** = `POCKETS_BASE`. ✔
- `Wallet = Pockets2 + 8×20 + 0x18 = 0x37D60` = `WALLET_OFF`. ✔

Les 0x18 octets qui suivent chaque bloc de 20 slots (`PlayerItemSet`) contiennent :
- un **u32 = nombre de slots disponibles** (0x37D48 pour la poche, doit valoir 20) ;
- **20 octets d'assignation à la roue d'items** (0x37D4C-0x37D5F, valeur 0..7, ou 0xFF si non assigné).

L'upstream et les loadouts (160 octets) n'y touchent pas : bien. Les slots 21-40 du sac ne sont pas gérés.

**Structure `Item` de NHSE (8 octets)** :

| Offset | Champ NHSE | Sens |
|---|---|---|
| +0 | `ItemId` (u16) | `0xFFFE` = vide (`NONE`), `0xFFFD` = extension de grand objet (`EXTENSION`), à ne jamais mettre dans une poche |
| +2 | `SystemParam` (u8) | bits 0-1 rotation, 0x04 enterré, 0x20 posé au sol, autres bits non nommés |
| +3 | `AdditionalParam` (u8) | **emballage** : type (bits 0-1), papier (bits 2-5), 0x40 « montrer l'item », 0x80 |
| +4 | `Count` (u16) | quantité pour les items empilables — **mais aussi** : `BodyType`/motif pour les items personnalisables (`FreeParam & 7`, …), **ID de recette** (DIY, bouteille à la mer), **ID de fossile**, gènes pour les fleurs (u32 sur +4..+7) |
| +6 | `UseCount` (u16) | compteur d'usure des **outils** |

Un item vide pour NHSE vaut `Delete() => RawValue = NONE`, soit **`FE FF 00 00 00 00 00 00`**.

**Sémantique de la quantité** : l'éditeur d'items de NHSE remplit une pile complète avec `currentItem.Count = (ushort)(max - 1);` (`ItemEditor.cs`, `L_Count_DoubleClick`, où `max` vient de `MaxCountByKind`, par exemple 50 ou 99). **Le jeu stocke donc vraisemblablement « quantité − 1 »** : 0 = 1 item. Ce n'est pas écrit en toutes lettres dans NHSE (aucun commentaire, et l'éditeur affiche la valeur brute), mais c'est la seule lecture cohérente avec une pile maximale de `max`. Conséquences pour l'upstream et pour la spec :
- l'upstream borne `g_newCount` à 1..99 (l.837, l.855) et l'écrit brut : « ×1 » donnerait **2** items, « ×99 » en donnerait 100, au-delà de la pile maximale de nombreuses catégories ;
- sur un item **non empilable**, le champ +4 n'est pas une quantité. Écrire 1 donne la **variante n° 1** d'un meuble personnalisable, la **recette n° 1** d'une fiche DIY, etc. La règle de la spec « Poser / Remplir : écrit quantité 1 » (UX_SPEC § 5.7) est donc probablement fausse. La valeur neutre est **0** ;
- la recherche upstream force `g_newCount = 1` (l.915), soit probablement 2 items ;
- **à vérifier en jeu** avant d'afficher « ×N » : poser un Nook Miles Ticket avec la valeur brute 9 et voir si le jeu affiche 10 (décision D4).

**Octets hérités (+2, +3, +6, +7)** : l'upstream n'écrit que +0 et +4 (l.641, l.714-715). Poser un item sur un slot occupé conserve donc :
- +3 : **l'item posé devient un cadeau emballé** si l'ancien occupant l'était ;
- +2 : drapeaux « enterré » ou « posé au sol » et rotation de l'ancien item. Effet en poche inconnu ;
- +6 : **usure** d'un ancien outil appliquée au nouvel item. Un outil en or neuf peut naître usé, et un item non-outil hérite d'un compteur sans signification ;
- « Vider » laisse aussi ces octets derrière l'ID `0xFFFE`, au lieu de l'enregistrement vide de NHSE.

Écrire l'enregistrement complet (`{id, 0x00, 0x00, count, 0x0000}` pour poser, `FE FF 00 00 00 00 00 00` pour vider) supprimerait ces risques. Cela reste une écriture UI dans le tampon, de même nature que le chargement de loadout qui recopie déjà 160 octets complets. Mais c'est un **écart par rapport à l'upstream**, et la spec a choisi l'inverse (UX_SPEC Q3) → décision D3.

**Plage d'ID saisissable** : la spec accepte 0000…FFFD. `0xFFFD` est `EXTENSION` (case fantôme d'un objet de plusieurs cases) : il faut **exclure FFFD** et limiter la plage à 0000…FFFC. L'upstream autorise jusqu'à 65534 = 0xFFFE (l.835).

---

## 4. Invariants et adaptateurs côté UI

### 4.1 Invariants que la nouvelle UI DOIT respecter

1. **I1 — Zone d'écriture.** L'UI n'écrit que dans `personal[0x37CA8 .. 0x37D48)` (20 × 8 octets) et dans `g_vals[0..3]`. Jamais dans `mainData`, `origP`, `origM`, `pHdr`, `mHdr`, ni ailleurs dans `personal`.
2. **I2 — Synchronisation avant `doSave`.** Immédiatement avant chaque `doSave()` : `g_slot` ∈ 1..20, et `g_newId`/`g_newCount` égaux au contenu du slot `g_slot` dans `personal` (point c).
3. **I3 — Un seul appelant.** `doSave()` n'est appelé que par `applyAll`. La restauration et le Doctor ne sont appelés que depuis le panneau Système, derrière le dialogue danger.
4. **I4 — Base reprise uniquement après un succès** (`SAVED`, `RESTORED`, `REPAIRED`). En cas d'échec, la Base et les modifs en attente restent intactes.
5. **I5 — Zéro modif en attente** pour lancer la restauration ou le Doctor.
6. **I6 — Backup non fiable.** Tout statut « BAK… » met `g_backupTrusted = false` pour la session et bloque la restauration. Un `doSave` réussi le remet à vrai, car le backup a été réécrit entièrement.
7. **I7 — Session douteuse.** Un échec du Doctor met `g_sessionTainted = true`, ce qui bloque Appliquer, la restauration et la reconstruction depuis `origP` jusqu'au relancement.
8. **I8 — Restauration validée.** La restauration n'est proposée que si `validateBackupFiles()` réussit.
9. **I9 — Version supportée.** L'édition n'est activée que si `pSz == 0x74A40 && mSz == 0x9B0E90` (save 3.0.0-3.0.3). Sinon : lecture seule, message « version de save non prise en charge ».
10. **I10 — Valeurs bornées.** `g_vals` est toujours borné avant l'appel (bornes upstream, plus le plafond Wallet selon D6). Les ID posés appartiennent à 0000…FFFC.
11. **I11 — Pas de lecture de l'argent dans les octets** de `personal`/`mainData` pendant l'édition : seulement `g_vals`.
12. **I12 — Loadout chargé via un tampon temporaire.** Copie dans `personal` seulement si 160 octets ont été lus (corrige l.896).
13. **I13 — Aucun nouvel appel à `fsFsCommit`, `writeNand*` ou `fsFileWrite`** sur la savedata en dehors des trois blocs existants.

### 4.2 Adaptateurs minimaux proposés

Toutes ces fonctions sont **nouvelles et côté UI**. Elles n'appellent que l'existant : `LE16`, `LE32`, `WriteLE16`, `CryptACNH`, `DecInt`, `CalcChecksum`, `Murmur3Hash`, `doSave`, ainsi que le bloc de restauration et celui du Doctor tels quels. Celles qui ont besoin des variables locales de `main()` (`fs`, `pHdr`, `origP`, `pSz`…) sont des **lambdas** déclarées après `doSave` (l.663), comme `doSave` lui-même.

```cpp
// ---- État UI ----
static uint8_t  g_basePockets[160];
static uint32_t g_baseVals[4];
static bool     g_backupTrusted  = true;   // I6
static bool     g_sessionTainted = false;  // I7
static bool     g_editSupported  = false;  // I9

enum class EngineResult { Ok, BackupFailed, NandFailed, MallocFailed, Unknown };

// ---- Fonctions libres ----
static void syncSaveSlotVars(const uint8_t* personal);            // I2 (= UX_SPEC § 6.3)
static void snapshotBase(const uint8_t* personal);                // memcpy 160 o + g_vals → Base
static void discardAll(uint8_t* personal);                        // méthode 1 du point d
static int  pendingCount(const uint8_t* personal);                // nb de slots ≠ Base + nb de valeurs ≠ Base
struct SlotDiff { int slot; uint8_t before[8]; uint8_t after[8]; };
struct ValDiff  { int idx;  uint32_t before; uint32_t after; };
static int  collectDiffs(const uint8_t* personal,
                         SlotDiff outSlots[20], int& nSlots,
                         ValDiff outVals[4],   int& nVals);       // point e
static bool writeSlotRecord(uint8_t* personal, int slot /*1..20*/,
                            uint16_t id, uint16_t rawCount,
                            bool fullRecord /* D3 */);            // refuse slot hors 1..20, id > 0xFFFC (sauf 0xFFFE)
static EngineResult parseSaveStatus(const char* s);              // "SAVED" / "BAK" / "NAND write failed" / "malloc failed"
static EngineResult parseRestoreStatus(const char* s);           // "RESTORED" / "Restore failed: ..."
static EngineResult parseDoctorStatus(const char* s);            // "REPAIRED" / "Repair NAND write failed!"
static bool saveLayoutSupported(size_t pSz, size_t mSz);         // I9 : 0x74A40 / 0x9B0E90
static int  regionHashReport(const uint8_t* plain, const Region* r, int n,
                             bool okOut[] /* n */);               // h : Murmur3Hash(plain+off+4,len) == LE32(plain+off)
static void poisonFieldsReport(const uint8_t* mainPlain, size_t mSz,
                               uint32_t out[8]);                  // LE32(main + 0x5B3D50 + i*0x74A40)

// ---- Lambdas dans main(), après doSave ----
auto applyAll = [&]() -> EngineResult {
    // préconditions : g_editSupported && !g_sessionTainted && pendingCount() > 0
    g_status_buf[0] = '\0';
    syncSaveSlotVars(personal);          // I2, rien entre cette ligne et doSave()
    doSave();                            // inchangé
    EngineResult r = parseSaveStatus(g_status_buf);
    if (r == EngineResult::Ok)           { snapshotBase(personal); g_backupTrusted = true; }
    if (r == EngineResult::BackupFailed) { g_backupTrusted = false; }
    return r;
};
auto rebuildFromOrig = [&]() -> void;    // méthode 2 du point d : l.746-755 sans NAND ; interdit si g_sessionTainted
auto validateBackupFiles = [&]() -> bool;// lecture backup_*.dat (même code que l.721-735) → copie → CryptACNH →
                                         // checksums EncInt des 4 champs (+ regionHashReport optionnel) ; aucune écriture
auto doRestore = [&]() -> EngineResult;  // corps l.721-769 déplacé tel quel (UX_SPEC § 6.6) + parseRestoreStatus
auto doDoctor  = [&]() -> EngineResult;  // corps l.779-812 déplacé tel quel ; échec ⇒ g_sessionTainted = true
```

Remarque sur `doRestore`/`doDoctor` : le déplacement à l'identique en lambdas (UX_SPEC § 6.6, Q5) ne change aucune instruction du moteur. Il se vérifie avec `git diff -w --color-moved`. Cet audit le juge compatible avec la règle « ne pas modifier l'écriture ». L'alternative (injecter `HidNpadButton_ZL/ZR` dans `k`) est plus fragile.

---

## 5. Audit des flux de `docs/ux/UX_SPEC.md`

| § spec | Constat | Recommandation |
|---|---|---|
| § 4.1, § 4.5, § 6.5 (Base, Annuler) | Correct : c'est la méthode 1 du point d. | Ajouter I1 et I7 comme préconditions. |
| § 4.4, § 6.4 (Appliquer, détection « SAVED ») | Correct, mais « SAVED » ne prouve pas le commit (l.652). Les 3 sous-cas de « BAK » sont indiscernables. | Traiter tout « BAK » comme un backup non fiable (I6). Mentionner la limite du commit (D2). |
| § 6.3 (`syncSaveSlotVars`) | **Validé** (point c). | Aucune. |
| § 6.2 « règle des 8 octets » / Q3 | Fidèle à l'upstream, mais l'héritage de +3 (cadeau) et +6 (usure) est risqué (point i). | Décision D3. |
| § 5.7, Q2 (quantité 1…99 brute, non empilable = 1) | La valeur stockée est probablement quantité − 1. Pour un non empilable, +4 n'est pas une quantité. | Décision D4. En attendant : écrire 0 pour un non empilable, et ne pas afficher « ×N » comme une quantité sûre. |
| § 5.2 (saisie d'ID 0000…FFFD) | 0xFFFD = `EXTENSION`. | Limiter à 0000…FFFC. |
| § 2.3, Q4 (Wallet ≤ 999 999 999) | Plafond in-game de 99 999 (connaissance du jeu, non trouvée dans NHSE). | Décision D6. |
| § 2.3 Système → Infos (`pRegC`, `mRegC`) | Ces compteurs peuvent venir du cache (l.572-573) et ne prouvent pas que les hash sont valides. | Afficher plutôt `regionHashReport` (point h). |
| § 4.3 Récap | Ne mentionne pas la réécriture de main.dat (Loan + 8 champs). | Ajouter une ligne fixe, éventuellement avec `poisonFieldsReport`. |
| § 5.9 Restauration | La garde « zéro modif » est bonne. Il manque la garde sur la fiabilité du backup et la validation de son contenu. | I6 et I8. |
| § 5.10 Doctor | « copie d'abord l'état actuel » : précision, il copie `origP`/`origM` (état validé), pas la RAM. Pas de gestion d'un échec. | I7 (`g_sessionTainted`). |
| § 7 « Écriture NAND échouée » | Le texte conseille « Système → Restaurer », mais la restauration est bloquée tant que des modifs sont en attente. Elle est d'ailleurs inutile : sans commit, la save validée n'a pas changé (sous l'hypothèse de journalisation). | Texte proposé : « La save de la console n'a pas été modifiée (écriture non validée). Vos modifs sont toujours en attente : réessayez Appliquer, ou annulez-les. » |
| § 6.9 Tests | Le test « le jeu charge la save » est précisément la question h. | Le faire **en premier**, avec le protocole du point h et un backup externe. |
| § 5.8 Loadouts | Le tampon temporaire corrige l.896 : correct. Un loadout recopie +2/+3/+6 de l'instantané : cohérent. | Aucune. |
| Q10 (Appliquer sans modif) | Réécrire « à vide » réencode `g_vals` (no-op) et met à zéro les 8 champs, ce qui ne change rien d'autre. | Garder caché ; derrière un dialogue danger si exposé. |

---

## 6. Décisions à prendre par l'utilisateur

Aucune de ces modifications n'a été faite. Les deux premières (D1, D2) toucheraient le moteur ; les autres sont des choix d'UI qui s'écartent de l'upstream.

### D1 — Recalculer les hash Murmur3 avant le chiffrement (modification du moteur)
- **Quoi** : dans `doSave`, entre l.641 et l.643, pour chaque région de `pReg`/`mReg` : `WriteLE32(out + off, Murmur3Hash(out + off + 4, len))`. Idem dans le Doctor (étape 2, l.793) et, éventuellement, une fois au chargement.
- **Pourquoi c'est peut-être inévitable** : si le jeu vérifie les hash (test du point h), aucune adaptation UI ne peut produire une save valide. Le hash dépend des octets écrits, et seul le moteur les connaît au moment de l'écriture.
- **Risque** : si une région est fausse (cache venant d'une autre version, ou d'une save déjà corrompue), on écrit 4 octets au mauvais endroit, en zone hashée. Atténuation : utiliser la **table fixe** NHSE `REV_300` (2 régions pour personal.dat, 19 pour main.dat, § 3.h) plutôt que les régions découvertes, et ne l'appliquer que si I9 est vrai.
- **À décider après** le test en jeu du point h. Si le jeu accepte des hash faux, ne rien faire.

### D2 — Vérifier le résultat de `fsFsCommit` (modification minime du moteur, optionnelle)
- **Quoi** : `if (R_FAILED(fsFsCommit(&fs))) { status "COMMIT failed"; ne pas resynchroniser }` à l.652, et de même à l.745 et l.809. Éventuellement, faire retourner un `bool` à `doSave`.
- **Pourquoi** : sinon, un commit échoué s'affiche « SAVED », la Base est reprise, et `origP` ne correspond plus à la NAND (point f).
- **Risque** : très faible (ajout d'une condition, aucune donnée écrite en plus). C'est toutefois une modification du moteur, d'où la décision. Sans elle, le cas reste indétectable.

### D3 — Écrire l'enregistrement de poche complet (UI seulement, écart par rapport à l'upstream)
- **Quoi** : poser = `{id, 0x00, 0x00, rawCount, 0x0000}` ; vider = `FE FF 00 00 00 00 00 00` (convention NHSE `Delete()`).
- **Pour** : supprime l'héritage de l'emballage (+3), des drapeaux (+2) et de l'usure (+6) de l'ancien occupant.
- **Contre** : s'écarte du comportement upstream éprouvé (+0/+4 seulement). Remettre +6 à 0 sur un outil réel le rend neuf, ce qui est sans doute acceptable. La spec a choisi de ne pas le faire (Q3).

### D4 — Convention « quantité − 1 » (UI seulement)
> **Confirmé en jeu le 12/11/2026 (ACNH 3.0.3)** : brut 9 = 10 tickets. La v1.0.0 était sortie avec `kCountZeroBased = false` et donnait un objet de trop ; corrigé en v1.0.1, qui borne aussi chaque pile à la limite du jeu (`item_stacks.txt`, d'après `MaxCountByKind` de NHSE). Une pile au-delà de la limite (50 bois) est ramenée par le jeu à une autre valeur (11 constatés), sans corruption.

- **Quoi** : afficher `raw + 1` pour les empilables et écrire `quantité − 1` ; écrire `0` pour les non empilables.
- **À vérifier en jeu d'abord** (point i). Le choix change le sens de toute la plage 1…99 de l'upstream et la règle « 1/slot » de la spec.

### D5 — Backup « de début de session » non écrasé (nouveau code d'écriture SD, hors moteur)
- **Quoi** : au premier Appliquer de la session, copier aussi `origP`/`origM` dans des fichiers distincts (par ex. `session_personal.dat` / `session_main.dat`), avec le même schéma que `wrFile` du Doctor (l.782-788).
- **Pourquoi** : le backup unique est écrasé à chaque Appliquer (§ 1.2), et la restauration n'archive pas l'état qu'elle remplace.
- **Risque** : espace SD (≈ 10,7 Mo de plus). Ne touche pas la save.

### D6 — Plafond du Wallet (UI seulement)
- **Quoi** : borner le Wallet à 99 999 (plafond in-game, d'après la connaissance du jeu : non trouvé dans NHSE), ou seulement avertir au-delà.
- L'upstream autorise 999 999 999 (l.825).

### Autres points à confirmer (pas de décision de code)
- **Loan** : `LOAN_OFF = 0x33CBF0` = champ `Loan` de la **maison n° 0** (`PlayerHouseList` + 0x10, NHSE `PlayerHouse1`). Que cette maison soit celle de `Villager0` est une supposition de l'upstream.
- **Journalisation de la savedata** : l'hypothèse « pas de commit = rien de modifié » (point f) est à confirmer.
- **8 champs « heal »** : leur signification est inconnue. À confirmer avec `poisonFieldsReport` (valent-ils déjà 0 sur une save saine ?).
- **Version de save** : offsets valables pour 3.0.0-3.0.3 seulement (I9). Le champ `SaveRevision` (u16 à 0x0E de l'en-tête, 31..34 pour 3.0.x) pourrait compléter le contrôle de taille. À vérifier : l'en-tête est-il en clair à cet endroit ?

---

## 7. Sources

**Code audité** : `source/main.cpp`, commit `731c228` (branche `power-pockets`, identique à l'upstream), et `README.md` l.12 et l.59.

**NHSE** (kwsch/NHSE, branche `master`) :
- Structure `Item`, `NONE`, `EXTENSION`, `Delete()`, `SystemParam`, `AdditionalParam`, `Count`, `UseCount` : https://raw.githubusercontent.com/kwsch/NHSE/master/NHSE.Core/Structures/Item/Item.cs
- `MaxCountByKind`, `TryGetMaxStackCount` : https://raw.githubusercontent.com/kwsch/NHSE/master/NHSE.Core/Structures/Item/ItemInfo.cs
- `currentItem.Count = (ushort)(max - 1);` (convention quantité − 1), Count utilisé comme ID de recette ou de fossile : https://raw.githubusercontent.com/kwsch/NHSE/master/NHSE.WinForms/Controls/ItemEditor.cs
- Offsets 3.0 (`PlayerOther = 0x37be0`, `Pockets1`, `Pockets2`, `Wallet`, `Bank`) : https://raw.githubusercontent.com/kwsch/NHSE/master/NHSE.Core/Save/Offsets/PersonalOffsets30.cs ; correspondance des révisions : https://raw.githubusercontent.com/kwsch/NHSE/master/NHSE.Core/Save/Offsets/PersonalOffsets.cs
- Bloc méta de 0x18 octets (compteur + assignation à la roue) : https://raw.githubusercontent.com/kwsch/NHSE/master/NHSE.Core/Editing/Inventory/PlayerItemSet.cs
- Bag = slots 21-40, Pocket = slots 1-20 : https://raw.githubusercontent.com/kwsch/NHSE/master/NHSE.Core/Save/Files/Personal.cs
- `GSaveLandOtherStart = 0x5b3d50`, `PlayerHouseList` : https://raw.githubusercontent.com/kwsch/NHSE/master/NHSE.Core/Save/Offsets/MainSaveOffsets30.cs
- `Loan` à `Data[0x10..]` : https://raw.githubusercontent.com/kwsch/NHSE/master/NHSE.Core/Structures/Villager/PlayerHouse1.cs
- Tables de régions de hash et tailles de fichiers `REV_300_PERSONAL = 0x74A40`, `REV_300_MAIN = 0x9B0E90` : https://raw.githubusercontent.com/kwsch/NHSE/master/NHSE.Core/Hashing/FileHashRevision.cs, https://raw.githubusercontent.com/kwsch/NHSE/master/NHSE.Core/Hashing/FileHashRegion.cs
- Recalcul des hash à chaque sauvegarde : https://raw.githubusercontent.com/kwsch/NHSE/master/NHSE.Core/Save/Meta/EncryptedFilePair.cs, https://raw.githubusercontent.com/kwsch/NHSE/master/NHSE.Core/Save/Meta/HorizonSave.cs
- `EncryptedInt32` (constante, rotation, checksum) : https://raw.githubusercontent.com/kwsch/NHSE/master/NHSE.Core/Encryption/EncryptedInt32.cs
- Révisions d'en-tête (`SaveRevision` à 0x0E) : https://raw.githubusercontent.com/kwsch/NHSE/master/NHSE.Core/Save/Meta/FileHeaderInfo.cs, https://raw.githubusercontent.com/kwsch/NHSE/master/NHSE.Core/Save/Meta/RevisionChecker.cs

**Limites de l'audit** : aucune exécution sur console. Les points marqués « à confirmer » (vérification des hash par le jeu, convention quantité − 1, journalisation de la savedata, sens des 8 champs « heal », plafond du Wallet, maison n° 0 = Villager0) reposent sur la lecture du code et de NHSE, pas sur un test en jeu.
