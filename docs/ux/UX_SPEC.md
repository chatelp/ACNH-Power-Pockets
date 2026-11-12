# ACNH Power Pockets — Spécification UX de la refonte

> Statut : **proposition v1**, à valider (voir § 9 « Questions ouvertes »).
> Portée : interface et ergonomie uniquement. Le moteur de sauvegarde (crypto AES, hash Murmur3, `applyEdit`, `doSave`, `backupToSd`, restauration du backup, Save Doctor, offsets) **n'est pas modifié** : l'UI l'appelle tel quel.
> Référence code : `source/main.cpp` de l'upstream cbreezy210/ACNH-Save-Editor (≈ 990 lignes), numéros de ligne cités `l.NNN`.
> Charte graphique : `docs/design/CHARTE.md` et `docs/design/mockups/` (session parallèle). Au moment de la rédaction ces fichiers n'existaient pas encore ; les noms de composants ci-dessous (§ 1.3) sont à réaligner sur la charte si elle en choisit d'autres. Les couleurs, tailles et positions relèvent de la charte, pas de ce document.

---

## Sommaire

1. Objectifs, principes, glossaire
2. Architecture en panneaux
3. Jeu de boutons
4. Modèle « BIOS » : préparer, relire, appliquer
5. Flux détaillés
6. Correspondance avec le code existant (plan d'implémentation)
7. Erreurs visibles par l'utilisateur
8. Écarts par rapport à l'upstream (pour le README)
9. Questions ouvertes
10. Checklist de couverture de la mission

---

## 1. Objectifs, principes, glossaire

### 1.1 Problèmes de l'UI actuelle

| Problème | Où dans l'upstream |
|---|---|
| Un appui sur **A** écrit immédiatement en NAND (avec backup). Pas de relecture possible de ce qui va être écrit. | `l.841-848`, `l.874` |
| **ZL** restaure le backup SD en NAND en **un seul appui**, sans confirmation. **ZR** deux fois lance le Doctor. Deux gâchettes « faciles » portent les actions les plus dangereuses. | `l.720-770`, `l.773-815` |
| Une seule modif de poche par écriture : `doSave` n'écrit que `g_newId/g_newCount` dans `g_slot` (plus le tampon). Remplir 10 slots = 10 sauvegardes NAND ou des allers-retours grille/favoris. | `l.640-641` |
| 8 boutons à mémoriser sur l'écran principal, chacun avec un rôle différent selon l'écran (X = favoris *ou* retour, B = charger un loadout *ou* désarmer le Doctor *ou* quitter la recherche). | `l.669-950` |
| La grille des 20 slots n'est atteignable que depuis les favoris ; la recherche ramène sur l'écran principal avec une quantité forcée à 1. | `l.859-864`, `l.912-918` |
| Favoris codés en dur (`FAV_NAMES`), non éditables ; modulo par zéro si la liste résolue est vide. | `l.143-168`, `l.850-851` |
| Aucune notion d'item non empilable : on peut écrire « golden axe ×99 ». | `l.837` |

### 1.2 Principes directeurs

1. **Rien n'est écrit tant qu'on n'a pas dit « Appliquer ».** Toutes les modifs (poches, argent, chargement de loadout) sont préparées en mémoire, comme dans un BIOS. Un seul écran, le **Récap**, montre tout ce qui va changer, avant → après.
2. **Peu de boutons, toujours le même rôle.** A ouvre, B ferme, L/R changent de panneau, + mène à la sortie (Récap), − annule. Le reste passe par des **menus d'actions**.
3. **Les actions qui écrivent directement en NAND sont isolées** dans le panneau Système, signalées en rouge, bloquées s'il y a des modifs en attente, et exigent une confirmation maintenue.
4. **Le chemin le plus fréquent est le plus court** : prendre un item, le poser dans plusieurs slots d'affilée, appliquer.
5. **Le moteur est intouchable** : l'UI ne fait que lire/écrire les mêmes variables que l'upstream (`g_vals[]`, tampon `personal` aux poches, `g_slot/g_newId/g_newCount`) et appelle `doSave()` une fois.

### 1.3 Glossaire (noms de composants proposés)

| Terme | Définition |
|---|---|
| **Panneau** | Une des 6 pages principales, sélectionnée par L/R. Équivaut aux onglets d'un BIOS. |
| **Barre d'onglets** | Rangée en haut listant les 6 panneaux, le panneau actif mis en évidence, avec les indications `L` et `R` aux extrémités. |
| **Puce « modifs »** | Indicateur global dans l'en-tête : `● 3 modifs` quand il y a des modifs en attente, `✓ À jour` sinon. |
| **Item en main** | Le « presse-papiers » de l'app : un item (ID + quantité) prêt à être posé dans des slots. Affiché en permanence dans l'en-tête (« En main : Nook Miles Ticket ×10 » ou « En main : — »). Remplace le « dernier item » de la demande. |
| **Grille de poches** | Les 20 slots du personnage (2 colonnes × 10, comme l'écran 2 upstream). |
| **Liste** | Liste verticale paginée (favoris, résultats de recherche, loadouts, valeurs d'argent, entrées système). |
| **Focus** | L'élément actuellement sélectionné dans le panneau (surbrillance). |
| **Menu d'actions** | Petite liste contextuelle ouverte par A sur l'élément focalisé. La 1ʳᵉ entrée est l'**action par défaut**. |
| **Stepper** | Modale d'édition d'un nombre : ←/→ petit pas, ↑/↓ grand pas, A valide, B rétablit la valeur d'avant. |
| **Dialogue** | Modale de confirmation (titre, texte, 2 ou 3 boutons). Variante **Dialogue danger** (rouge, confirmation par A maintenu). |
| **Toast** | Message court en bas d'écran (3 s), vert = succès, ambre = avertissement, rouge = erreur. Remplace la ligne `g_status`. |
| **Récap** | Écran listant toutes les modifs en attente avant → après, avec Appliquer / Annuler tout / Quitter. |
| **Marque « modifié »** | Signe visuel (point + liseré) sur un slot ou une valeur qui diffère de l'état chargé. |
| **Badge « 1/slot »** | Pastille sur tout item non empilable. |
| **Badge ★** | Pastille sur tout item présent dans les favoris (visible dans le Catalogue). |

---

## 2. Architecture en panneaux

### 2.1 Gabarit d'écran commun (1280 × 720)

```
┌──────────────────────────────────────────────────────────────────────┐
│ ACNH Power Pockets          En main : Nook Miles Ticket ×10   ● 3 modifs │  en-tête
│ [L]  Poches · Favoris · Catalogue · Argent & Miles · Loadouts · Système [R] │  barre d'onglets
├──────────────────────────────────────────────────────────────────────┤
│                                                                      │
│                     contenu du panneau actif                         │
│                                                                      │
├──────────────────────────────────────────────────────────────────────┤
│ (toast éventuel)                                                     │
│ Ⓐ Actions  Ⓨ Poser+suivant  Ⓑ Retour  Ⓛ/Ⓡ Panneau  ⊕ Récap  ⊖ Annuler │  pied d'aide
└──────────────────────────────────────────────────────────────────────┘
```

- Le **pied d'aide** liste toujours les mêmes boutons, dans le même ordre ; seuls les libellés de A et Y changent selon le focus (ex. « Ⓨ Prendre » dans Favoris). Un bouton sans effet dans le contexte est grisé, pas masqué (positions stables).
- La **barre d'onglets** affiche un point `•` à côté d'un panneau qui contient des modifs en attente (Poches, Argent & Miles).
- Le panneau actif et le focus de chaque panneau sont **mémorisés** : revenir dans un panneau retrouve le même élément sélectionné.

### 2.2 Ordre des panneaux et justification

| # | Panneau | Rôle |
|---|---|---|
| 1 | **Poches** *(ouverture)* | Là où tout aboutit : voir et modifier les 20 slots. |
| 2 | **Favoris** | Source d'items rapide (liste courte, triée A→Z). |
| 3 | **Catalogue** | Source d'items exhaustive (recherche clavier dans `items.txt`). |
| 4 | **Argent & Miles** | Wallet, Bank, Miles, Loan. |
| 5 | **Loadouts** | 5 configurations de poches sur la SD. |
| 6 | **Système** | Infos, backup, actions directes NAND (restauration, Doctor), Quitter. |

Justification :
- **Poches à l'ouverture** : c'est l'usage principal de l'app (« Power Pockets ») ; l'écran affiche immédiatement l'état réel de l'inventaire, ce qui rassure (on voit qu'on a bien chargé la bonne save) et sert de point d'arrivée de tous les flux d'items.
- **Favoris puis Catalogue juste à droite** : ce sont les deux sources d'items ; R ×1 / R ×2 depuis les Poches. Les favoris passent avant le catalogue car ils couvrent la majorité des usages en moins d'appuis (pas de clavier).
- **Argent & Miles au milieu** : usage ponctuel, indépendant des poches.
- **Loadouts** après : usage plus rare, et lié aux poches mais pas aux sources d'items.
- **Système tout au bout** : il contient les seules actions qui écrivent directement en NAND. Le placer à l'extrémité (et **sans bouclage** L/R, voir § 3.3) garantit qu'on n'y atterrit jamais par un appui de trop depuis les Poches.

### 2.3 Contenu de chaque panneau

#### Panneau 1 — Poches
- **Grille de poches** 2 × 10 : colonne gauche slots 1-10, colonne droite slots 11-20 (même disposition que l'écran 2 upstream, `l.366-378`). Chaque cellule : numéro, nom (tronqué), `×quantité`, badge `1/slot` si non empilable, marque « modifié » si différent de l'état chargé. Slot vide : « (vide) ».
- **Volet détail** (à droite ou en bas selon la charte) pour le slot focalisé : nom complet, ID hexadécimal (`16A2`), quantité, état « original → nouveau » si modifié.
- Le focus initial est le slot 1.

#### Panneau 2 — Favoris
- **Liste** des favoris (intégrés + utilisateur, voir § 5.6), triée A→Z sans tenir compte de la casse (même tri que `favAlphaLess`, `l.229`), 8 lignes par page, indicateur `Page 1/3`.
- Chaque ligne : nom, ID hex, badge `1/slot` le cas échéant.
- État vide : « Aucun favori. Ajoutez-en depuis le Catalogue (Ⓐ → Ajouter aux favoris). »

#### Panneau 3 — Catalogue
- Ligne **Recherche** en tête de liste : `Rechercher : [ springy ]` ; A ou Y dessus ouvre le clavier natif (`showKeyboard`, `l.272`).
- **Résultats** : liste paginée (6 à 8 par page selon la charte), compteur « 42 résultats ». Chaque ligne : ID hex, nom, badges `★` et `1/slot`.
- La dernière recherche et la position dans les résultats sont conservées quand on change de panneau.
- Sans recherche : texte d'aide « Ⓐ pour saisir un nom (ex. "star"). »

#### Panneau 4 — Argent & Miles
- **Liste** de 4 valeurs : Wallet, Bank, Miles, Loan (prêt immobilier), chacune affichée avec séparateurs de milliers et, si modifiée, « 12 000 → 99 999 ».
- Bornes identiques à l'upstream (`l.825-829`) : Wallet et Bank 0 → 999 999 999 ; Miles et Loan 0 → 9 999 999.

#### Panneau 5 — Loadouts
- **Liste** des 5 emplacements (`LOADOUT_NAMES`, `l.171`) : nom, `[Vide]` ou `[Enregistré — 14/20 items]` (même calcul que `checkLoadouts`, `l.597-618`).
- Volet aperçu optionnel : les 20 slots du loadout focalisé en lecture seule (lecture du fichier `l_N.dat`, 160 octets).

#### Panneau 6 — Système
- **Infos** : version, dossier SD `sdmc:/switch/acnh_power_pockets/`, présence et taille des fichiers `backup_personal.dat` / `backup_main.dat` (et date de modification si disponible via `stat`), nombre d'items chargés depuis `items.txt`, nombre de favoris, nombre d'entrées `nonstackable.txt`, nombre de régions détectées (`pRegC`, `mRegC`).
- **Actions** (liste) :
  1. *Recharger favoris et liste 1/slot depuis la SD*
  2. *Restaurer le backup SD → NAND* — **danger** (rouge)
  3. *Save Doctor (réparer la save)* — **danger** (rouge)
  4. *Aide : boutons*
  5. *Quitter l'app*
- Bandeau permanent en haut du panneau : « ⚠ Les actions en rouge écrivent **directement** dans la save de la console, sans passer par le Récap. »

---

## 3. Jeu de boutons

### 3.1 Tableau global (valable dans tous les panneaux)

| Bouton | Rôle constant | Détail |
|---|---|---|
| **D-pad / stick gauche** | Naviguer | Déplace le focus. En **Stepper** : ←/→ petit pas, ↑/↓ grand pas. |
| **A** | Ouvrir / valider | Hors modale : ouvre le **menu d'actions** de l'élément focalisé (1ʳᵉ entrée présélectionnée → « A, A » = action par défaut). Dans une modale : valide l'entrée/le bouton focalisé. |
| **Y** | Action rapide | Exécute **directement** l'action par défaut de l'élément focalisé, sans menu. Toujours identique à la 1ʳᵉ entrée du menu d'actions, et toujours réversible (jamais d'écriture disque/NAND). |
| **B** | Retour / fermer | Ferme la modale courante (menu, stepper → rétablit la valeur, dialogue → choix « Annuler »). Hors modale : sans effet (pas de « retour » entre panneaux, on navigue avec L/R). |
| **L / R** | Panneau précédent / suivant | Désactivés pendant une modale. Pas de bouclage. |
| **+** | Récap | Ouvre l'écran **Récap** (Appliquer / Annuler tout / Quitter). C'est aussi la porte de sortie de l'app : conforme à la convention homebrew « + = quitter », mais en passant par le Récap. |
| **−** | Annuler toutes les modifs | S'il y a des modifs en attente : dialogue « Annuler les N modifs ? » (focus par défaut sur « Non »). Sinon : toast « Aucune modif à annuler ». |
| **X** | — | Aucune action (réservé). |
| **ZL / ZR** | — | **Aucune action.** Ne déclenchent plus jamais la restauration ni le Doctor. |
| **HOME** | Système | Comportement Switch standard (non intercepté). Quitter par HOME abandonne les modifs non appliquées, rien n'est écrit. |

### 3.2 Action par défaut (Y) selon le focus

| Focus | Ⓨ (action rapide) | Libellé pied d'aide |
|---|---|---|
| Slot de poche, item en main défini | Poser l'item en main **et avancer au slot suivant** | Ⓨ Poser+suivant |
| Slot de poche, rien en main | Prendre en main (pipette) | Ⓨ Prendre |
| Ligne de favori / résultat | Prendre en main (quantité en main courante) et revenir aux Poches | Ⓨ Prendre |
| Ligne Recherche (Catalogue) | Ouvrir le clavier | Ⓨ Saisir |
| Valeur (Argent & Miles) | Ouvrir le stepper | Ⓨ Modifier |
| Loadout | Charger dans les poches (modif en attente) | Ⓨ Charger |
| Entrée Système | *Aucune* (Y grisé : les actions Système passent toujours par le menu A puis un dialogue) | — |

### 3.3 Détails de navigation

- **Répétition à l'appui long** (D-pad, et ←/→/↑/↓ dans le stepper) : reprendre la cadence upstream (`l.682-684` : répétition après 20 frames, puis toutes les 5 frames) ; dans le stepper, ajouter une **accélération** : après 2 s maintenues, le pas est ×10 (voir § 5.3).
- **Pas de bouclage L/R** : R sur Système et L sur Poches ne font rien (petit retour visuel « bord »). Raison : rendre la position de Système prévisible et éviter qu'un L depuis Poches y mène.
- **Bouclage vertical** dans les listes : oui (comme l'upstream `l.707-708`, `l.850-851`), sauf dans la grille de poches où ↑ sur le slot 1 / ↓ sur le slot 10 restent en place et ←/→ changent de colonne (slot n ↔ n±10).
- **Pagination** des listes : ↓ sur la dernière ligne d'une page passe à la page suivante (comportement upstream). Pas de bouton de page dédié (L/R sont réservés aux panneaux).
- **Modales empilables au plus sur 2 niveaux** (ex. menu d'actions → stepper). B ferme un niveau à la fois.

### 3.4 Dialogue danger

- Fond/cadre rouge, titre « Écriture directe en NAND ».
- Deux boutons : `Annuler` (focus par défaut) et `Maintenir Ⓐ pour confirmer`.
- La confirmation exige de **maintenir A ~1,5 s** sur le bouton de confirmation (jauge de progression) ; relâcher avant la fin annule la jauge. Un appui bref ne fait rien. Cela remplace le « ZR armé, ZR encore » de l'upstream et empêche les doubles appuis accidentels.

---

## 4. Modèle « BIOS » : préparer, relire, appliquer

### 4.1 Les trois états de données

| État | Contenu | Où en mémoire |
|---|---|---|
| **Original console** | Ce qui est en NAND (chiffré) depuis la dernière lecture/écriture. Sert au backup SD. | `origP`, `origM` (moteur, inchangé) |
| **Base** (nouveau, UI) | Copie déchiffrée des 160 octets de poches + des 4 valeurs, prise au chargement et après chaque écriture réussie. Référence pour « modifié ? » et pour Annuler. | `g_basePockets[160]`, `g_baseVals[4]` |
| **Travail** | Ce que l'utilisateur prépare. | Poches : `personal + POCKETS_BASE` (20 × 8 o). Argent : `g_vals[4]`. |

**Modifs en attente** = toute différence entre Travail et Base :
- un slot `i` est modifié si ses octets **+0..+1 (ID) ou +4..+5 (quantité)** diffèrent de la base (on compare les 8 octets, mais l'UI n'écrit jamais que ces deux champs, § 6.2) ;
- une valeur `v` est modifiée si `g_vals[v] != g_baseVals[v]`.

Le compteur de la puce = nombre de slots modifiés + nombre de valeurs modifiées. Une modif ramenée manuellement à sa valeur d'origine **disparaît** du compteur (comparaison d'état, pas historique d'actions).

### 4.2 Indicateurs

- Puce en-tête : `● N modifs` (couleur accent) / `✓ À jour`.
- Point `•` sur l'onglet du panneau concerné.
- Marque « modifié » sur chaque slot/valeur, avec « avant → après » dans le volet détail.
- Le pied d'aide met en évidence `⊕ Récap` quand N > 0.

### 4.3 Écran Récap (bouton +)

Plein écran modal (L/R désactivés).

```
Récapitulatif — 4 modifications
───────────────────────────────────────────────
POCHES
  Slot 3   (vide)               →  Nook Miles Ticket ×10
  Slot 4   Pomme ×5             →  Nook Miles Ticket ×10
  Slot 12  golden axe ×1        →  (vide)
ARGENT & MILES
  Wallet   12 000               →  99 999
───────────────────────────────────────────────
Appliquer écrit personal.dat et main.dat dans la save de la console.
Le backup SD sera d'abord remplacé par l'état actuel de la console.

 [ Appliquer ]  [ Appliquer et quitter ]  [ Tout annuler ]  [ Quitter sans appliquer ]  [ Retour ]
```

- ↑/↓ parcourt les lignes de modif puis la rangée de boutons. A sur une ligne de modif → menu « Rétablir cette modif » / « Aller à l'élément » (ferme le Récap et focalise le slot/la valeur).
- **Sans modif en attente** : le Récap affiche « Aucune modification en attente » et seulement `[ Quitter ]` (focus) et `[ Retour ]`. *Appliquer* est masqué (voir Q10).
- `[ Appliquer ]` : exécute la séquence § 4.4. Le Récap **est** la confirmation : pas de dialogue supplémentaire.
- `[ Appliquer et quitter ]` : § 4.4 puis quitte **seulement si** l'écriture a réussi ; sinon reste sur le dialogue d'erreur.
- `[ Tout annuler ]` : § 4.5 (dialogue de confirmation).
- `[ Quitter sans appliquer ]` : dialogue « Quitter et perdre les N modifs ? Rien ne sera écrit. » `[Rester]` (focus) / `[Quitter]`.
- B ou `[ Retour ]` : ferme le Récap.

### 4.4 Appliquer

1. Afficher un écran d'attente « Écriture en cours… ne pas éteindre » (rendu une frame avant l'appel, l'appel est bloquant).
2. **Synchroniser** les variables du moteur depuis le tampon (§ 6.3, `syncSaveSlotVars()`).
3. Vider `g_status_buf`, appeler **une fois** `doSave()` (inchangé).
4. Lire le résultat dans `g_status_buf` (§ 6.4) :
   - **succès** (`SAVED…`) → reprendre la **Base** depuis le tampon (le moteur a re-déchiffré `personal` à partir de ce qu'il a écrit) et depuis `g_vals` ; toast vert « Modifications appliquées. Backup SD mis à jour avec l'état précédent. » ; puce `✓ À jour`.
   - **échec** → dialogue d'erreur (§ 7) ; **les modifs restent en attente**, rien n'est perdu.

### 4.5 Annuler (bouton − ou Récap → Tout annuler)

- Dialogue : « Annuler les N modifications ? Les poches et l'argent reviennent à l'état chargé. Rien n'est écrit. » `[Non]` (focus) / `[Oui, tout annuler]`.
- Effet : recopier `g_basePockets` → `personal + POCKETS_BASE` et `g_baseVals` → `g_vals`. **Aucune écriture NAND, aucun appel au moteur, aucun re-déchiffrement** (seules ces zones sont modifiées par l'UI).
- L'item en main est **conservé** (ce n'est pas une donnée de la save).
- Toast « Modifications annulées ».

### 4.6 Quitter

- Chemins : Récap → Quitter / Appliquer et quitter ; Système → Quitter l'app.
- Sans modif en attente : quitte immédiatement (le `break` de la boucle principale, comme `l.843`).
- Avec modifs en attente : dialogue « N modifications non appliquées » `[Appliquer et quitter]` / `[Quitter sans appliquer]` / `[Rester]` (focus sur **Rester**).

---

## 5. Flux détaillés

Notation : `R` = appui sur R, `Ⓐ→«X»` = A ouvre le menu d'actions puis choix de l'entrée « X ».

### 5.1 Remplir 10 slots avec le même item (« coller le dernier item »)

> **Révision après validation du plan (implémentée dans la v1.0.0) — elle remplace la décision ci-dessous.**
> On ne pose **jamais** sur un slot occupé. **Y = poser l'item en main dans le prochain slot *vide*** à partir du slot focalisé (avec bouclage 20 → 1), puis le focus passe au slot vide suivant : 10 appuis remplissent les 10 prochains slots vides sans jamais écraser un item. « Poser ici » est supprimé (Y pose déjà dans le slot focalisé s'il est vide). Pour remplacer un item : « Vider le slot » puis Y.
> La pose écrit l'enregistrement propre `{id, 0, 0, quantité, 0}` ; « Vider » écrit l'enregistrement vide NHSE `FE FF 00 00 00 00 00 00` (règle des 8 octets du § 6.2 révisée en conséquence : plus d'octets hérités, puisque seuls des slots vides sont écrits).
> Non empilable : valeur brute 0 écrite à la pose (et non 1, cf. audit point i). Convention de quantité centralisée dans `kCountZeroBased` (`source/pp_model.h`) : **tranchée le 12/11/2026, `true`** (9 tickets écrits en brut 9 donnaient 10 en jeu). Depuis la v1.0.1, le pas de quantité est aussi borné par la pile max de chaque objet (`item_stacks.txt`, tiré de NHSE).

**Décision initiale (remplacée) : Y sur un slot = poser l'item en main puis avancer automatiquement au slot suivant.** La variante sans avance existe dans le menu (« Poser ici »).

Justification :
- Remplir N slots coûte **N appuis** au lieu de 2N (poser + descendre) ; c'est le cas d'usage phare (10 tickets, 10 sacs de clochettes…).
- Le risque de l'avance automatique (dépasser et écraser un slot) est neutralisé par le modèle BIOS : tout reste en mémoire, chaque slot écrasé porte la marque « modifié », apparaît dans le Récap avec son contenu d'avant, et se rétablit individuellement (menu « Rétablir l'original ») ou en bloc (−).
- L'ordre d'avance est l'ordre numérique 1 → 20 (colonne gauche de haut en bas, puis colonne droite) ; **pas de saut** des slots occupés (prévisible : « je vois où je pose ») ; **arrêt** au slot 20 (toast « Dernier slot »), pas de bouclage.
- Pour « remplir tout ce qui est vide », une action dédiée existe (« Remplir les slots vides »), qui elle ne touche qu'aux slots vides.

Flux A — 10 Nook Miles Tickets ×10 dans les slots 5 à 14 :
1. Écran Poches. `R` → Favoris.
2. ↓ jusqu'à « Nook Miles Ticket ». `Ⓐ→«Prendre avec quantité…»` → stepper initialisé à la quantité en main courante (défaut 1). ↑ (+10) → 11, ← → 10. `A`.
3. L'app repasse automatiquement sur **Poches** (focus sur le dernier slot focalisé). Toast « En main : Nook Miles Ticket ×10 — Ⓨ pour poser ». En-tête mis à jour.
4. Naviguer jusqu'au slot 5. `Y` ×10 → slots 5…14 remplis, focus sur le slot 15. Puce `● 10 modifs`.
5. `+` → Récap → `[ Appliquer ]`.

Flux B — remplir tous les slots vides :
1. Item en main défini (comme ci-dessus).
2. Sur n'importe quel slot : `Ⓐ→«Remplir les slots vides avec l'item en main»` → dialogue « Remplir 7 slots vides avec Nook Miles Ticket ×10 ? » `[Oui]`/`[Non]`.
3. Toast « 7 slots remplis ».

Flux C — reprendre un item déjà présent dans les poches (pipette) :
- Sur le slot source : `Ⓐ→«Prendre en main»` (ou `Y` si rien n'est en main). La quantité en main devient celle du slot (1 si non empilable, § 5.7).

### 5.2 Choisir un item puis le placer

**Via le Catalogue (recherche)**
1. `R R` → Catalogue. Focus sur la ligne Recherche.
2. `A` (ou `Y`) → clavier natif, texte initial = dernière recherche. Saisir « star », valider.
3. Résultats (recherche insensible à la casse, sous-chaîne, comme `l.924-933`). Focus sur le 1ᵉʳ résultat. Aucun résultat : « Aucun item ne contient "xyz". »
4. Sur « star fragment » : `Y` = prendre en main avec la quantité en main courante (bornée, voir § 5.7) **ou** `Ⓐ→«Prendre avec quantité…»` (stepper).
5. Retour automatique aux Poches ; poser avec `Y` (avance) ou `Ⓐ→«Poser ici»`.

Menu d'actions d'un résultat : `Prendre en main` · `Prendre avec quantité…` · `Ajouter aux favoris` / `Retirer des favoris` · `Infos (ID, 1/slot)`.

**Via les Favoris** : identique à partir de l'étape 4, depuis le panneau Favoris (`R`).

**Saisie directe d'un ID (fonction « expert » héritée de la ligne Item upstream, `l.835`)** : sur un slot, `Ⓐ→«Saisir un ID (hex)…»` → clavier natif, saisie `16A2` → l'item est **mis en main** (pas posé), avec contrôle 0000…FFFD (`FFFE` = vide est réservé à « Vider »). ID hors `items.txt` : accepté, affiché « Inconnu (XXXX) », avertissement ambre.

**Quantité en main** : mémorisée entre deux prises ; valeur initiale **1** (comme l'upstream). Elle se règle via « Prendre avec quantité… » ou `Ⓐ→«Quantité en main…»` sur un slot (stepper 1…99).

### 5.3 Changer une quantité

Dans un slot existant :
1. Poches, focus sur le slot. `Ⓐ→«Quantité…»` → stepper initialisé à la quantité actuelle.
2. ← / → : −1 / +1 ; ↓ / ↑ : −10 / +10 (pas de la grille upstream `l.837`). Bornes **1…99** (inchangées). Appui long : répétition, puis pas ×10 après 2 s.
3. `A` valide (slot marqué modifié si ≠ base). `B` rétablit la valeur d'avant l'ouverture du stepper.
- Slot vide : entrée grisée « Quantité… (slot vide) ».
- Item non empilable : entrée grisée « Quantité : 1 (non empilable) » ; voir § 5.7.

Argent & Miles :
1. `R R R` → Argent & Miles. Focus sur Wallet. `Y` (ou `Ⓐ→«Modifier»`) → stepper.
2. ← / → : ∓1 000 ; ↓ / ↑ : ∓100 000 (pas upstream `l.827`). Appui long : répétition puis ×10. Bornes upstream (§ 2.3).
3. Autres entrées du menu : `Saisir au clavier…` (clavier numérique natif, valeur bornée, saisie invalide → toast ambre et valeur inchangée) · `Mettre au maximum` · `Mettre à 0` (libellé « Rembourser le prêt (0) » pour Loan) · `Rétablir l'original` (si modifié).

### 5.4 Vider un slot

1. Poches, focus sur le slot. `Ⓐ→«Vider le slot»`.
2. Effet immédiat en mémoire : ID `FFFE`, quantité `0` (exactement les valeurs upstream `l.711-715`). Marque « modifié », toast « Slot 7 vidé (en attente) ».
3. Réversible : `Ⓐ→«Rétablir l'original»` sur le slot, ou − (tout annuler).
- Pas de dialogue : l'action est en mémoire et réversible.
- Entrée grisée si le slot est déjà vide.

### 5.5 Rétablir un slot

Sur un slot marqué modifié : `Ⓐ→«Rétablir l'original»` → recopie les 8 octets du slot depuis `g_basePockets`. Disponible aussi depuis une ligne du Récap.

### 5.6 Ajouter / retirer un favori

Sources : liste intégrée `FAV_NAMES` (22 noms, inchangée) + surcouche utilisateur `sdmc:/switch/acnh_power_pockets/user_favorites.txt`.

**Ajouter** — depuis le Catalogue (ou depuis un slot de poche : `Ⓐ→«Ajouter aux favoris»`) :
1. Focus sur un résultat sans badge `★`. `Ⓐ→«Ajouter aux favoris»`.
2. Écriture immédiate du fichier `user_favorites.txt` (ce n'est **pas** une donnée de la save : pas de passage par le Récap). Badge `★` affiché, toast « Ajouté aux favoris ».
3. Limite atteinte (64) : dialogue « Limite de 64 favoris atteinte. Retirez-en un d'abord. » `[OK]`.

**Retirer** — depuis Favoris (ou un résultat `★` du Catalogue) :
1. `Ⓐ→«Retirer des favoris»` → dialogue court « Retirer "gold roses" des favoris ? » `[Non]`/`[Oui]` (un favori intégré retiré ne réapparaît qu'avec « Restaurer les favoris par défaut »).
2. Le focus passe à la ligne suivante (ou précédente si c'était la dernière). Liste vide → état vide § 2.3.

**Restaurer les favoris par défaut** : dernière entrée du menu d'actions de toute ligne de Favoris → dialogue → supprime `user_favorites.txt` (après l'avoir renommé en `user_favorites.bak`).

**Format `user_favorites.txt`** (texte UTF-8, une instruction par ligne, appliquées dans l'ordre) :
```
# ACNH Power Pockets — favoris utilisateur
+16A2 Nook Miles Ticket      ← ajoute l'ID 16A2 (le nom n'est qu'indicatif)
-0A4E gold roses             ← masque l'ID 0A4E de la liste intégrée
```
- Ligne = `+` ou `-`, 4 chiffres hex, espace, nom libre. `#` = commentaire, lignes vides ignorées.
- L'ID fait foi (deux items peuvent partager un nom). Les favoris intégrés sont d'abord résolus par nom (`findItemIdByName`, `l.222`), puis la surcouche s'applique par ID.
- Réécriture **atomique** à chaque changement : écrire `user_favorites.tmp`, puis supprimer l'ancien et renommer. Le fichier réécrit ne contient que des lignes valides normalisées.

### 5.7 Items non empilables

**Source** : liste intégrée (outils, meubles… à définir, voir Q6) + surcouche `sdmc:/switch/acnh_power_pockets/nonstackable.txt` :
```
# une entrée par ligne : ID hex préfixé 0x, ou nom exact d'items.txt
0x0A1B
golden axe
-0x16A2        ← retire un ID de la liste intégrée
```
Lignes invalides ignorées et comptées (§ 7). Fichier en lecture seule pour l'app (édité à la main ; un bouton d'édition dans l'UI est une question ouverte, Q7).

**Règles**
| Situation | Comportement |
|---|---|
| Affichage partout (slot, favoris, résultats, détail) | Badge `1/slot`. |
| Prendre en main un item non empilable | Quantité en main forcée à **1** (toast discret si la quantité en main était > 1 : « Quantité ramenée à 1 (non empilable) »). |
| Stepper de quantité (slot ou « Prendre avec quantité… ») | Entrée grisée « Quantité : 1 (non empilable) » ; le stepper ne s'ouvre pas. |
| Poser / Remplir les slots vides | Écrit quantité **1**. |
| Pipette sur un slot non empilable dont la save contient ×5 | Item en main ×1. |
| Slot déjà présent dans la save avec une quantité > 1 | **Affiché tel quel** (`×5`, avec le badge `1/slot` et une icône ⚠ « quantité inhabituelle ») et **écrit tel quel** tant que l'utilisateur ne le modifie pas. Aucune correction automatique. |
| Chargement d'un loadout | Contenu chargé tel quel (c'est un instantané de poches, pas une action de quantité ; voir Q8). |

### 5.8 Loadouts

**Sauver** (écrit sur la SD, pas dans la save) :
1. `R ×4` → Loadouts. Focus sur « Mining ». `Ⓐ→«Enregistrer les poches actuelles ici»`.
2. Si l'emplacement existe : dialogue « Remplacer "Mining" (14/20 items) ? » `[Non]`/`[Oui]`.
3. Écrit les 160 octets **du tampon de travail** (donc les poches telles qu'affichées, modifs en attente comprises — mention dans le dialogue : « Les poches affichées, y compris les modifs non appliquées, seront enregistrées. »).
4. Toast « Loadout "Mining" enregistré » ; liste rafraîchie.

**Charger** (= modif en attente) :
1. Focus sur un loadout `[Enregistré]`. `Y` ou `Ⓐ→«Charger dans les poches»`.
2. Si des slots ont déjà des modifs en attente : dialogue « Charger "Mining" remplace les 20 slots, y compris vos modifs de poches en attente. » `[Annuler]`/`[Charger]`. Sinon pas de dialogue (réversible par −).
3. Lecture du fichier dans un **tampon temporaire** de 160 octets ; si et seulement si 160 octets sont lus, copie dans `personal + POCKETS_BASE`.
4. Retour automatique aux Poches ; slots différents marqués modifiés ; toast « Loadout "Mining" chargé — + pour appliquer ».
- Emplacement vide : entrée grisée « Charger (vide) ».
- Autres entrées : `Supprimer le loadout` (dialogue) — optionnel, voir Q9.

### 5.9 Restaurer le backup SD (panneau Système)

> **Écrit directement en NAND.** Remplace la save de la console par `backup_personal.dat` + `backup_main.dat`.

1. `R ×5` → Système. Focus sur « Restaurer le backup SD → NAND » (rouge).
2. `A` →
   - **S'il y a des modifs en attente** : l'action est **bloquée**. Dialogue « Vous avez N modifs non appliquées. Appliquez-les (+) ou annulez-les (−) avant de restaurer. » `[OK]`. *Raison* : la restauration recharge entièrement les tampons ; mélanger avec des modifs préparées est source de confusion.
   - **Sinon**, si les fichiers de backup sont absents (vérification préalable par `stat`) : dialogue d'info « Aucun backup sur la SD. » `[OK]`.
   - **Sinon** : **Dialogue danger** :
     « Restaurer le backup SD ?
     La save de la console sera **remplacée immédiatement** par le backup (fait automatiquement avant votre dernière application, date : JJ/MM HH:MM).
     Tout ce qui a été appliqué depuis sera perdu. Le backup lui-même n'est pas modifié. »
     `[Annuler]` (focus) / `[Maintenir Ⓐ pour restaurer]`.
3. Confirmation → écran « Restauration… » → appel du bloc de restauration upstream inchangé (§ 6.6).
4. Succès (`RESTORED…`) → Base reprise depuis le tampon rechargé ; toast vert « Save restaurée depuis le backup SD ». Échec → dialogue d'erreur (§ 7).

### 5.10 Save Doctor (panneau Système)

> **Écrit directement en NAND, sans rafraîchir le backup SD.** Ré-encode Wallet/Bank/Miles/Loan et réécrit les fichiers ; copie d'abord l'état actuel dans `damaged_personal.dat` / `damaged_main.dat` (copie médico-légale).

1. Système, focus sur « Save Doctor (réparer la save) ». `A`.
2. **Modifs en attente → bloqué** (même dialogue qu'en 5.9). *Raison impérative* : le Doctor écrit le tampon `personal` courant (`l.808`), donc il écrirait les modifs de poches en attente **sans backup SD**.
3. Sinon **Dialogue danger** :
   « Lancer le Save Doctor ?
   À n'utiliser que si le jeu refuse de charger la save après une modification.
   • Une copie de l'état actuel est faite dans damaged_*.dat.
   • La save est réécrite immédiatement.
   • Le backup SD n'est **pas** mis à jour. »
   `[Annuler]` (focus) / `[Maintenir Ⓐ pour réparer]`.
4. Appel du bloc Doctor upstream inchangé (§ 6.6). Succès (`REPAIRED…`) → Base reprise ; dialogue « Réparation terminée. Quittez complètement l'app puis lancez le jeu. » ; variante sans copie médico-légale : avertissement ambre. Échec : § 7.

### 5.11 Quitter

Voir § 4.6. Depuis Système → `Ⓐ→«Quitter l'app»` ou via + → Récap.

---

## 6. Correspondance avec le code existant (plan d'implémentation)

### 6.1 Zones intouchables (ne pas modifier, ne pas réordonner)

| Zone | Lignes |
|---|---|
| `XorShift128`, `GetParam`, AES (`sbox`, `KeyExpansion`, `AesEncryptBlock`), `CryptACNH` | l.17-104 |
| Murmur3 (`Scramble`, `Advance`, `Finalize`, `Murmur3Hash`, `GetLength`) | l.106-122 |
| Helpers LE / checksum / `DecInt` / `EncInt` | l.124-132 |
| Offsets `WALLET_OFF`, `BANK_OFF`, `MILES_OFF`, `LOAN_OFF`, `POCKETS_BASE` | l.135-139 |
| `applyEdit`, `readSaveFile`, `backupToSd` | l.177-209 |
| Montage save, lecture, copie `origP/origM`, déchiffrement, régions, layout cache, décodage `g_vals` | l.509-591 |
| `doSave` | l.620-663 |
| Corps de la restauration | l.721-769 |
| Corps du Doctor (branche « armé ») | l.778-812 |
| Garde Applet | l.440-468 |

Tout le reste (`FAV_*`, `buildFavorites`, `itemName`, `showKeyboard`, `buildUI`, boucle d'input, loadouts) est de l'UI et peut être réécrit.

### 6.2 Ce que chaque action modifie

| Action utilisateur | Zone modifiée | Écriture disque/NAND |
|---|---|---|
| Poser (Y / « Poser ici » / Remplir) | `WriteLE16(personal + POCKETS_BASE + (s-1)*8 + 0, id)` et `WriteLE16(… + 4, qty)` | Aucune |
| Quantité… | `WriteLE16(… + 4, qty)` | Aucune |
| Vider | `+0 ← 0xFFFE`, `+4 ← 0` (identique `l.714-715`) | Aucune |
| Rétablir l'original (slot) | `memcpy(rec, g_basePockets + (s-1)*8, 8)` | Aucune |
| Valeur Argent & Miles | `g_vals[0..3]` (0 Wallet, 1 Bank, 2 Miles, 3 Loan) | Aucune |
| Prendre en main / pipette / quantité en main | `g_handId`, `g_handCount`, `g_handValid` (nouveaux) | Aucune |
| Charger loadout | `fread` → `uint8_t tmp[160]` puis `memcpy(personal + POCKETS_BASE, tmp, 160)` | Aucune |
| Enregistrer loadout | lecture `personal + POCKETS_BASE` (160 o) | SD `l_N.dat` |
| Favori ± | vecteur `g_favs` (nouveau) | SD `user_favorites.txt` |
| Annuler tout | `memcpy(personal + POCKETS_BASE, g_basePockets, 160)` ; `memcpy(g_vals, g_baseVals, 16)` | Aucune |
| Appliquer | `syncSaveSlotVars()` puis `doSave()` | SD backup + NAND (moteur) |
| Restaurer backup | bloc moteur | NAND (moteur) |
| Save Doctor | bloc moteur | SD `damaged_*` + NAND (moteur) |

**Règle des 8 octets** : un enregistrement de poche fait 8 octets ; l'UI n'écrit **que** l'ID (+0, u16) et la quantité (+4, u16), exactement comme l'upstream. Les octets +2, +3, +6, +7 ne sont jamais touchés (ils gardent la valeur de l'item qui occupait le slot ; voir Q3). Seuls « Rétablir l'original », « Annuler tout » et « Charger loadout » recopient des enregistrements complets — et ils recopient des octets qui proviennent eux-mêmes de la save ou d'un instantané de la save.

### 6.3 Point crucial : `doSave()` et `g_slot / g_newId / g_newCount`

`doSave` (l.620-663) :
1. sauvegarde `origP/origM` sur la SD ;
2. copie le tampon `personal` dans `outP` ;
3. applique `g_vals` ;
4. **écrit `g_newId` / `g_newCount` dans le slot `g_slot` de `outP`** (l.640-641) ;
5. chiffre, écrit, commit, met à jour `origP/origM`, re-déchiffre dans `personal`, et réécrit encore `g_newId/g_newCount` dans `personal` au slot `g_slot` (l.655-656).

Dans la nouvelle UI, la vérité des poches est **le tampon** ; `g_slot`, `g_newId`, `g_newCount` ne servent plus d'état d'édition. Si on les laissait désynchronisés, l'étape 4 écraserait le slot `g_slot` avec une valeur périmée. D'où l'invariant :

> **Ces trois variables ne sont écrites qu'à un seul endroit : `syncSaveSlotVars()`, appelée immédiatement avant `doSave()`.**

```cpp
// UI — juste avant chaque appel à doSave()
static void syncSaveSlotVars(const uint8_t* personal) {
    if (g_slot < 1 || g_slot > 20) g_slot = 1;
    const uint8_t* r = personal + POCKETS_BASE + (g_slot - 1) * 8;
    g_newId    = LE16(r);       // ID réel du slot
    g_newCount = LE16(r + 4);   // quantité réelle du slot
}
```

Avec cette synchronisation, les écritures l.641 et l.656 réécrivent des valeurs identiques à celles du tampon : **no-op**. `g_slot` peut valoir le slot focalisé (ou 1) ; c'est sans effet.

Autres lieux où l'upstream écrit ces variables (à ne pas reproduire dans la nouvelle UI) : l.590, l.711-712, l.757 (dans le bloc de restauration : laissé tel quel, sans conséquence puisque la restauration recharge le tampon et que `syncSaveSlotVars()` repasse avant tout `doSave()`), l.833, l.835, l.837, l.860-861, l.899-900, l.914-915.

### 6.4 Détecter le résultat de `doSave()` sans le modifier

`doSave` ne retourne rien ; il écrit son verdict dans `g_status_buf`. L'UI :
```cpp
g_status_buf[0] = '\0';
syncSaveSlotVars(personal);
doSave();
bool ok = strncmp(g_status_buf, "SAVED", 5) == 0;
```
| `g_status_buf` | Signification | Origine |
|---|---|---|
| `SAVED personal + main to NAND!` | Succès | l.657 |
| `BAK sd open 0x…` | Carte SD non ouvrable, **rien écrit** | l.196 via l.621 |
| `BAK write failed!` | Écriture du backup échouée, **rien écrit en NAND** | l.207 via l.621 |
| `NAND write failed!` | Écriture NAND échouée (pas de commit) | l.658 |
| `malloc failed!` | Mémoire insuffisante, rien écrit | l.659 |

Même principe pour la restauration (`RESTORED`, `Restore failed: …`) et le Doctor (`REPAIRED`, `Repair NAND write failed!`). L'UI traduit ces chaînes en dialogues (§ 7) ; `g_status_buf` n'est plus affiché brut.

Après succès : `memcpy(g_basePockets, personal + POCKETS_BASE, 160); memcpy(g_baseVals, g_vals, sizeof g_vals);`.

### 6.5 Prise de la Base au démarrage

Juste **après** le bloc moteur l.530-591 (succès de lecture), et sans rien y insérer :
```cpp
if (personal && pHdr && mainData && mHdr && origP && origM) {
    memcpy(g_basePockets, personal + POCKETS_BASE, 160);
    memcpy(g_baseVals, g_vals, sizeof g_vals);
    g_saveReady = true;
}
```
Si `g_saveReady` est faux, l'app affiche l'écran d'erreur de montage (§ 7) et aucun panneau d'édition n'est actif (même condition que la garde upstream l.686).

### 6.6 Déclencher restauration et Doctor depuis le panneau Système

Les deux blocs sont aujourd'hui inline dans la branche `g_screen == 0` de la boucle d'input. Recommandation (**à valider, Q5**) : **déplacer verbatim** chaque corps dans une lambda déclarée juste après `doSave`, sans changer une seule instruction :
- `auto doRestore = [&]() { /* corps l.721-769, identique */ };`
- `auto doDoctor  = [&]() { /* corps l.779-812, identique (sans la gestion de g_repairArmed) */ };`

La revue se fait avec `git diff -w --color-moved` : le diff ne doit montrer que du déplacement/de l'indentation. `g_repairArmed` (l.774-777, l.816-820) est remplacé par le Dialogue danger de l'UI.

Après l'appel : succès → reprise de la Base (§ 6.4) ; `g_newId/g_newCount` modifiés par l.757 sont ignorés (cf. § 6.3).

### 6.7 Nouvel état UI (proposé)

```cpp
enum Panel { P_POCKETS, P_FAVS, P_CATALOG, P_MONEY, P_LOADOUTS, P_SYSTEM, P_COUNT };
static int      g_panel = P_POCKETS;
static int      g_focus[P_COUNT];            // focus mémorisé par panneau
static uint8_t  g_basePockets[160];
static uint32_t g_baseVals[4];
static bool     g_saveReady = false;
static bool     g_handValid = false;
static uint16_t g_handId = 0xFFFE;
static int      g_handCount = 1;             // 1..99
static std::vector<Fav2> g_favs;             // remplace FAVS/FAV_COUNT (Fav2 { uint16_t id; std::string label; bool builtin; })
static std::vector<bool> g_nonStack;         // indexé par ID, taille g_allItems.size() (ou std::unordered_set<uint16_t>)
// pile de modales : menu d'actions, stepper, dialogue, dialogue danger, récap, attente
```

Fonctions UI (toutes hors moteur) : `slotRec(s)`, `slotModified(s)`, `pendingCount()`, `placeHand(s)`, `clearSlot(s)`, `setSlotQty(s,q)`, `revertSlot(s)`, `discardAll()`, `applyAll()` (= § 4.4), `syncSaveSlotVars()`, `isNonStackable(id)`, `loadFavorites()`, `saveUserFavorites()`, `loadNonStackable()`, `loadoutLoad(i)`, `loadoutSave(i)`, `buildUI()` réécrit par panneau.

Les listes `FAV_NAMES`, `LOADOUT_NAMES`, `findItemIdByName`, `favAlphaLess`, `itemName`, `showKeyboard`, `checkLoadouts` sont réutilisées.

### 6.8 Ordre d'implémentation suggéré

1. État UI + Base + `syncSaveSlotVars` + `applyAll` / `discardAll`, sur l'UI actuelle (permet de tester le modèle BIOS isolément).
2. Gabarit d'écran (en-tête, onglets, pied d'aide, toasts) et navigation L/R.
3. Panneau Poches (grille, détail, menu d'actions, stepper, item en main, Y poser+avancer).
4. Favoris (liste dynamique, surcouche SD) et Catalogue (recherche existante, badges).
5. Argent & Miles (stepper + clavier numérique).
6. Récap + Quitter.
7. Loadouts (tampon temporaire).
8. Système (extraction verbatim restauration/Doctor, Dialogue danger).
9. Non empilables (liste + règles), erreurs § 7, textes finaux.
10. Mise à jour README (§ 8).

### 6.9 Vérification de non-régression du moteur (pour l'implémentation)

- `git diff upstream/main -- source/main.cpp` restreint aux zones § 6.1 : vide (ou déplacement pur pour § 6.6, vérifié avec `--color-moved`).
- Test sur console : modifier 3 slots + Wallet, Appliquer, relancer l'app → valeurs relues identiques ; le jeu charge la save ; `backup_*.dat` = état d'avant.
- Test « slot g_slot » : modifier uniquement le slot 20 alors que `g_slot` vaut 1 → après Appliquer, slot 1 inchangé et slot 20 modifié.
- Annuler tout → aucune date de modification de fichier ne change sur la SD ni en NAND.

---

## 7. Erreurs visibles par l'utilisateur

| Cas | Détection | Ce que voit l'utilisateur | État après |
|---|---|---|---|
| **`font.ttf` manquant** | `TTF_OpenFont` renvoie `nullptr` (l.475-477) — sinon l'écran reste vide | Avant l'init SDL (ou en repli), écran console comme la garde Applet : « font.ttf introuvable dans sdmc:/switch/acnh_power_pockets/. Copiez-le depuis l'archive de l'app. Ⓐ pour quitter. » | App quittée, rien écrit. |
| **`items.txt` manquant** | `fopen` échoue (l.490) | Bannière ambre persistante : « items.txt introuvable : noms d'items indisponibles. » Les poches affichent les IDs (« ID 16A2 »). Favoris et Catalogue : état vide explicatif. L'édition des poches (pipette, quantités, vider, saisie d'ID) et de l'argent reste possible. | Fonctionnel en mode dégradé. |
| **Save non montable** (jeu ouvert) | `fsOpenSaveDataFileSystem` échoue (l.526) | Écran bloquant : « Impossible d'ouvrir la save. Fermez complètement Animal Crossing (HOME → X), puis relancez l'app via le titre du jeu en maintenant R. » Ⓐ Quitter. | Aucun panneau d'édition. |
| **Fichiers save illisibles** | l.591 | Écran bloquant « Lecture de la save impossible (personal.dat / main.dat). » | Idem. |
| **Backup SD impossible** à l'Appliquer | `BAK…` | Dialogue erreur : « Le backup sur la carte SD a échoué (carte pleine, protégée ou absente ?). **Rien n'a été écrit dans la save.** Vos modifications sont toujours en attente. » `[OK]` | Modifs en attente intactes. |
| **Écriture NAND échouée** | `NAND write failed!` | Dialogue erreur : « L'écriture de la save a échoué. Le moteur n'a pas validé (commit) l'écriture. Le backup SD contient l'état d'avant. Ne lancez pas le jeu tout de suite : réessayez Appliquer, ou utilisez Système → Restaurer. » `[OK]` | Modifs en attente intactes. |
| **Mémoire insuffisante** | `malloc failed!` | « Mémoire insuffisante, rien n'a été écrit. Relancez l'app en mode Application (titre + R). » | Modifs intactes. |
| **Restauration** : backup absent / taille différente / lecture / NAND | `Restore failed: …` (l.724-760) | Dialogue avec la cause en clair (« Aucun backup », « Le backup ne correspond pas à cette save », « Lecture du backup impossible », « Écriture de la save échouée »). | Save inchangée sauf échec NAND (même message que ci-dessus). |
| **Doctor** : échec NAND / pas de copie médico-légale | `Repair NAND write failed!` / `REPAIRED (no forensic copy)…` | Dialogue rouge / avertissement ambre. | — |
| **`user_favorites.txt` corrompu** (lignes invalides, IDs hors `items.txt`) | Au chargement | Toast ambre : « user_favorites.txt : 3 lignes ignorées. » Avant la première réécriture, l'original est copié en `user_favorites.bak`. | Lignes valides appliquées. |
| **`user_favorites.txt` non inscriptible** | Échec écriture/renommage | Toast rouge « Impossible d'enregistrer les favoris sur la SD. » La modification est annulée en mémoire. | Favoris inchangés. |
| **Liste de favoris vide** | `g_favs.empty()` | État vide du panneau (§ 2.3). Aucune navigation ni modulo (corrige le `% FAV_COUNT` upstream l.850). | — |
| **Favori intégré introuvable dans `items.txt`** | `findItemIdByName` → `0xFFFE` (l.226) | Le favori est **masqué** (et compté dans Système → Infos « 2 favoris intégrés introuvables »). Upstream l'affichait avec l'ID FFFE, ce qui revenait à proposer « vider ». | — |
| **Limite de favoris** (64) | `g_favs.size() >= 64` | Dialogue « Limite de 64 favoris atteinte. Retirez-en un d'abord. » | — |
| **`nonstackable.txt` corrompu / absent** | Au chargement | Absent : liste intégrée seule, silencieux. Lignes invalides : toast ambre « nonstackable.txt : N lignes ignorées ». | — |
| **Loadout** : fichier absent / tronqué / écriture courte | `fopen`, `fread != 160`, `fwrite != 160` | Toasts rouges « Loadout introuvable », « Loadout endommagé (taille incorrecte) : rien n'a été chargé », « Écriture du loadout incomplète ». | Tampon de travail **jamais** partiellement écrasé (tampon temporaire). |
| **Recherche sans résultat** | `g_searchResults.empty()` | « Aucun item ne contient "…" ». | — |
| **Saisie d'ID ou de valeur invalide** | Analyse du texte du clavier | Toast ambre « Valeur invalide », rien ne change. | — |
| **Lancement en mode Applet** | l.446 | Inchangé (écran console upstream). | — |

---

## 8. Écarts par rapport à l'upstream (pour le README)

> Version implémentée : voir le README (sections « Panels and controls » et « Changelog »). Différences avec le tableau ci-dessous : Y pose dans le **prochain slot vide** (§ 5.1 révisé), « Poser ici » n'existe pas, la restauration a une variante « backup de début de session », et le panneau System affiche des diagnostics en lecture seule (hash de régions, champs « heal »).

| Bouton / comportement | Upstream | Power Pockets |
|---|---|---|
| **A** | Sauvegarde immédiate en NAND (ou quitte sur « Quit App ») | Ouvre le menu d'actions de l'élément sélectionné. N'écrit jamais. |
| **+** | Ouvre/ferme la recherche | Ouvre le **Récap** : Appliquer (seule écriture de la save), Annuler tout, Quitter. |
| **−** | Vide le slot courant | Annule toutes les modifs en attente (avec confirmation). Vider un slot : A → « Vider le slot ». |
| **X** | Favoris / retour | Aucune action. Favoris = panneau (R depuis Poches). |
| **Y** | Loadouts | Action rapide : poser l'item en main et passer au slot suivant (Poches), prendre un item (Favoris/Catalogue), charger (Loadouts), modifier (Argent). |
| **B** | Retour, charger un loadout, désarmer le Doctor selon l'écran | Toujours : fermer / annuler la modale courante. |
| **L / R** | Gros pas (±100 000, ±256 d'ID, ±10 de quantité, ±5 slots) | Panneau précédent / suivant. Les gros pas passent sur ↑/↓ dans le stepper. |
| **← / →** | Petit pas | Navigation ; petit pas dans le stepper. |
| **ZL** | **Restaure le backup SD en NAND en 1 appui** | Aucune action. Restauration : Système → Restaurer (confirmation maintenue, bloquée si modifs en attente). |
| **ZR** | **Save Doctor** (2 appuis) | Aucune action. Doctor : Système → Save Doctor (même protection). |
| Sauvegarde | 1 slot par écriture, écriture à chaque A | Toutes les modifs préparées puis **une** écriture via Récap → Appliquer. Backup SD toujours fait avant (moteur inchangé). |
| Quitter | Ligne « Quit App » + A | + → Récap → Quitter (confirmation si modifs en attente), ou Système → Quitter. |
| Recherche | Ramène sur l'écran principal avec quantité 1 | Met l'item « en main » avec la quantité choisie, retour aux Poches. |
| Favoris | 22 noms codés en dur | 22 intégrés + ajout/retrait depuis l'UI (`user_favorites.txt`, max 64). |
| Grille des slots | Seulement depuis les favoris | Panneau d'ouverture. |
| Non empilables | Aucun contrôle | Badge « 1/slot », quantité forcée à 1 sur toute action utilisateur (`nonstackable.txt`). |
| Édition directe de l'ID (ligne « Item » ±1/±256) | Oui | Remplacée par « Saisir un ID (hex)… » dans le menu d'un slot. |
| Charger un loadout | Écrit dans le tampon, puis « press A to save » | Devient une modif en attente visible dans le Récap ; fichier tronqué n'abîme plus le tampon. |
| Nouveaux fichiers SD | — | `user_favorites.txt` (+ `.tmp`/`.bak`), `nonstackable.txt` (optionnel). Le dossier est `sdmc:/switch/acnh_power_pockets/` (décision du 2026-11-11 : dossier et `.nro` propres à Power Pockets, rien n'est partagé avec l'upstream). |

Inchangé : moteur de sauvegarde, emplacement et format des backups (`backup_personal.dat`, `backup_main.dat`), loadouts `l_0..4.dat`, `layout_*.dat`, `damaged_*.dat`, bornes des valeurs (quantité 1…99, Wallet/Bank ≤ 999 999 999, Miles/Loan ≤ 9 999 999), garde mode Applet.

---

## 9. Questions ouvertes

| # | Question | Proposition par défaut |
|---|---|---|
| Q1 | **Langue de l'UI** : français, anglais, ou les deux (fichier de chaînes) ? L'upstream est en anglais et sa communauté aussi. Le `font.ttf` fourni contient-il les caractères accentués (é, è, à, ç, →, •, ★, ⚠) ? | Chaînes centralisées dans une table, anglais par défaut, français en option ; vérifier la police (sinon remplacer les symboles par des équivalents ASCII). |
| Q2 | **Sémantique de la quantité** : la valeur brute à +4 est-elle la taille de pile ou « pile − 1 » (convention de certains éditeurs) ? | Garder les valeurs brutes de l'upstream (1…99) sans conversion ; à vérifier en jeu avant d'afficher « ×N ». |
| Q3 | **Octets +2/+3/+6/+7** non réécrits quand on pose un item sur un slot occupé : ils gardent les drapeaux de l'ancien item (emballage, variante…). Faut-il les remettre à zéro en posant ? | Non, garder exactement l'upstream (règle « ne pas toucher au moteur ») ; à surveiller en test. |
| Q4 | **Plafond Wallet** : le jeu plafonne le porte-monnaie à 99 999 ; l'upstream autorise 999 999 999. Ajouter un avertissement visuel au-delà de 99 999 ? | Bornes inchangées + mention ambre « au-delà du maximum du jeu (99 999) ». |
| Q5 | **Extraction verbatim** des blocs restauration/Doctor en lambdas (§ 6.6) : acceptable au regard de la règle « ne jamais toucher » ? Alternative sans aucun déplacement : laisser les blocs en place et les déclencher par un drapeau injectant `HidNpadButton_ZL/ZR` dans `k`, ce qui est plus fragile. | Extraction verbatim, revue avec `git diff -w --color-moved`. |
| Q6 | **Contenu de la liste intégrée « 1/slot »** : quelles catégories (outils, meubles, vêtements, fossiles…) ? `items.txt` ne porte pas de catégorie. | Démarrer avec les outils (dont les 6 outils en or), couronnes et meubles courants ; l'utilisateur complète via `nonstackable.txt`. |
| Q7 | **Éditer la liste 1/slot depuis l'UI** (menu « Marquer comme non empilable ») ? | Non pour la v1 (fichier édité à la main). |
| Q8 | **Loadouts et non empilables** : clamper à 1 au chargement ? | Non, charger tel quel. |
| Q9 | **Loadouts** : permettre de les renommer / supprimer / en avoir plus de 5 ? | Supprimer : oui (dialogue). Renommer et > 5 : plus tard. |
| Q10 | **Appliquer sans modif** : l'upstream permettait de réécrire la save « à vide » (ce qui ré-applique `g_vals` et le correctif main.dat de `doSave`). Garder une entrée « Réécrire la save maintenant » dans Système ? | Masqué dans le Récap ; entrée Système optionnelle « Réécrire la save (sans modif) » derrière un Dialogue danger. |
| Q11 | **Retour automatique aux Poches** après « Prendre en main » : toujours, ou option ? | Toujours (le geste suivant est toujours « poser »). |
| Q12 | **Limite de favoris** : 64 convient-il ? | 64 (8 pages de 8). |
| Q13 | **Recherche par ID** dans le Catalogue (taper `16A2` ou `0x16A2`) ? | Oui, si la requête est un hex de 4 chiffres, ajouter l'item correspondant en tête des résultats. |
| Q14 | **Pas de l'argent** : ±1 000 / ±100 000 hérités ; les Miles bénéficieraient d'un pas plus fin. | Garder l'upstream + clavier numérique pour les valeurs exactes. |

---

## 10. Checklist de couverture de la mission

| Point demandé | Section |
|---|---|
| 1. Architecture en panneaux L/R, ordre, ouverture, contenu | § 2 |
| 2. Jeu de boutons minimal et constant, menus contextuels, ZL/ZR neutralisés | § 3 |
| 3. Modèle BIOS : indicateur, Récap avant → après, Appliquer = un `doSave()`, Annuler sans NAND, Quitter avec confirmation | § 4 |
| 4. Flux : 10 slots (avance automatique tranchée), recherche/favoris, quantité, vider, favoris ±, non empilables, loadouts, restauration et Doctor dans Système | § 5 |
| 5. Correspondance code : `g_vals[]`, `personal + POCKETS_BASE`, `g_slot/g_newId/g_newCount`, appel de `doSave()` sans le modifier, règle des 8 octets | § 6 |
| 6. Erreurs visibles | § 7 |
| 7. Questions ouvertes et écarts upstream | § 9, § 8 |
