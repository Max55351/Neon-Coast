# Neon Coast hra – projekt na ploše

## Lodě, podvodní povrchy a jachta — 8. října 2026

- Původní `SM_NC2_Yacht` existoval, ale měl jen základní geometrii. Nová upravitelná 38m jachta má 19 178 trojúhelníků, uzavřený tvarovaný trup, tři paluby, panoramatické prosklení, zábradlí, schody, koupací plošinu, lehátka, posezení a radar. Není to sken skutečné jachty ani dokončený obytný interiér.
- Nové opakovatelné textury korálů a teakové paluby mají vlastní albedo a normálové mapy. Paluba používá UV souřadnice modelu, aby textura při pohybu neklouzala po světových souřadnicích. Korály a řasy mají oboustranné materiály; mořské skály používají existující kamenné PBR textury. Hladina je vykreslovaná i při pohledu zespodu.
- Staré komponentové přepisy materiálů se na lodích při změně typu vyprázdní. Modely lodí s různými druhy povrchů nepoužívají Nanite cestu pro průhledné sekce. Tmavé lodní prosklení je záměrně neprůhledná stylizovaná náhrada, nikoli simulované optické sklo.
- Podvodní efekt nemá původní velmi mělkou hloubku ostrosti na 4 metry; zmenšuje se vinětace a bloom. Nenahrazuje to skutečně chybějící geometrii dna.
- Nová kontrola uložených assetů odhalila, že dřívější hlášená přiřazení některých náhradních slotů nepřetrvala v souborech. Skripty nyní explicitně zapisují změněné struktury zpět do pole; tento běh používá přímo `StaticMesh.set_material`. Starší počty v přehledech níže nejsou důkazem správných všech uložených slotů.
- Sestavení kódu lodí a podvodního efektu uspělo (5 kroků). Hra ani PIE se na přání uživatele nespouštěly. Blenderový náhled ukazuje model, nikoli ověřený vzhled ve hře.
- Zálohy: `Saved/MarineBackup_20261008`, `Saved/MarineCodeBackup_20261008`. Přehled nasazení: `Saved/MarineInstall_20261008.json`.
- Dokončené nasazení: 54 přiřazených materiálových slotů na 37 modelech, bez hlášené chyby. Nový proces editoru přečetl všech 54 uložených slotů: žádná neshoda ani chyba (`Saved/MarineSavedSlots_20261008.json`). Jachta má šest platných materiálů a uloženou povrchovou kolizi. To potvrzuje uložené assety, nikoli funkčnost kolizí při hraní.
- Upravitelný model, FBX, náhled a zdrojové PNG jsou přímo ve složce hry `SourceAssets/Marine`. Původní model zůstal v záloze. Existující odkaz jachty v mapě používá nové tělo pod původním názvem `SM_NC2_Yacht`.

## Opakované nastupování, plavecká póza a povrchy — 8. října 2026

- Další oprava výstupu z aut měří celou karoserii včetně transformace a měřítka, hledá volné místo mimo ni a kontroluje skutečnou kolizi kapsle ještě před odpojením postavy. Pokud bezpečné místo nenajde, hráč zůstává uvnitř; neumisťuje ho naslepo na střechu nebo do jiného objektu. Platí pro řidiče i spolujezdce. Není to záruka výstupu z libovolné zaklíněné nebo převrácené polohy.
- Změna oblečení odstraňuje materiálové přepisy minulého obleku. Pokud náhradní vzhled nemá platné tělo, základní model se neschová. Závěrečné sestavení těchto změn uspělo (5 kroků).
- Offline oprava našla a obnovila 460 materiálových slotů podle existujících pojmenovaných materiálů. Nejde jen o šedou univerzální náhradu. Přehled: `Saved/MaterialRepair_20261008.json`; původní soubory jsou v `Saved/MaterialBackup_20261008`. Dalších 191 původních slotů nemělo odpovídající materiál v projektu a původní podklady se nepodařilo dohledat.
- Následný běh doplnil všech těchto 191 slotů: dohledatelné číslované aliasy oceánu/terénu použily existující materiály, ostatní použily 61 nových instancí v `Materials/RepairFallbacks` s rodičem vybraným podle druhu povrchu a pojmenované barvy. Přehled obsahuje původní slot, nový materiál a jeho rodiče: `Saved/MaterialFallback_20261008.json` (191 přiřazení, bez hlášené chyby). Tyto náhrady nejsou obnovené ztracené originály a netvoří chybějící geometrii. Vizuální vzhled se bez hraní neověřoval.
- F / gamepad Y se nyní váže pouze na trvalý `NCPlayerController`, nikoli na dočasné vstupní komponenty aut, lodí a postavy. Server podle aktuálně ovládaného prostředku rozhodne o vystoupení, nástupu nebo výstupu spolujezdce. Duplicitní mapování této klávesy byla odstraněna. Ostatní mapování pohybu zůstávají zachovaná.
- Plavecká póza odvozuje natočení trupu od referenční kostry konkrétního modelu, jeho skutečné transformace a směru pohybu. Již nepředpokládá shodnou lokální osu pánve u všech obleků. Postava při plavání ve třetí osobě natáčí tělo podle horizontální rychlosti. Jde nadále o procedurální pózu, nikoli novou profesionální plaveckou animaci.
- Obnova povrchových kolizí pokrývá také terénní dlaždice a další silniční modely, nejen původní most. Zapíná fyzikální i dotazovací kolizi, oboustranné dotazy a hlásí modely bez trojúhelníkové kolize do logu. Netýká se oceánu ani simulovaných pohyblivých těles.
- Editor bez hraní přestavěl a uložil všech 18 cílových assetů: 16 dlaždic terénu, `SM_NC_Roads` a `SM_NC_Bridge_Bascule`. Ukládá přesnou povrchovou kolizi, oboustranné fyzikální dotazy a zapnuté kolize sekcí. Nanite fallback má 100 % trojúhelníků a nulovou relativní chybu, aby fyzikální náhradní síť nebyla příliš zjednodušená. Zpřístupnění geometrie na CPU a podrobnější fallback mohou zvýšit spotřebu paměti; jde o opravu přesnosti, nikoli prokázané zrychlení.
- Záloha původních assetů je `Saved/SurfaceBackup_20261008`. Přehled uložených souborů: `Saved/SurfaceRepair_20261008.json` (18 uložených, žádná hlášená chyba). První pokus kvůli chybně přečtené cestě skript nespustil; až opakovaný běh úpravy provedl. Tato úprava nepřidává chybějící polygony ani nové textury.
- Překlad a propojení upraveného modulu uspěly (25 kroků). Hraní ani vizuální kontrola se na přání uživatele neprováděly. Bez snímku a polohy nelze potvrdit opravu všech skutečných geometrických děr.

## Nabídka Esc a uložené nastavení — 6. října 2026

- **Esc** při běžném hraní otevře nabídku **Pokračovat / Nastavení / Ovládání / Vypnout hru**. Šipky nebo myš vybírají, Enter potvrzuje. Esc na hlavní stránce nabídky pokračuje ve hře; v podnabídce se vrací zpět. Ukončení hry vyžaduje potvrzení.
- Nastavení: čtyři úrovně grafiky, měřítko vykreslování 50/60/70/85/100 %, limit 30/60/90/120 FPS nebo bez limitu a V-Sync. Enter nebo kliknutí přepíná hodnotu, změna se aplikuje a ukládá. Textury zůstávají na kvalitě 3 i u lehčích předvoleb; měřítko vykreslování nemění rozměry okna.
- Zástupce na ploše již nevnucuje původní grafické konzolové hodnoty při každém spuštění. Zachovává vypnuté rozmazání pohybu. Původní zástupce je zálohovaný v `Saved/NeonCoast_before_pause_settings_20261006.lnk`.
- V samostatné hře nabídka požádá engine o pozastavení světa, v multiplayeru otevře pouze místní nabídku: ostatní hráči a provoz pokračují. Pokračování obnovuje ovládání a skryje kurzor.
- Pokud je otevřená mapa, obchod, tuning nebo multiplayerový panel, první Esc zavře tento panel, další otevře nabídku. V cutscéně Esc nadále přeskočí scénu; následné Esc otevře běžnou nabídku.
- Při prvním spuštění této verze se dosavadní vynucený limit 60 FPS převede do uložených nastavení, pokud předtím nebyl nastaven jiný kladný limit. Další volby včetně „bez limitu“ se tímto převodem nepřepisují.
- Úplné sestavení (38 kroků) i závěrečné sestavení (5 kroků) uspělo; nasazené zdroje se shodují s upravenými kopiemi. Hra se na přání uživatele nespouští, ověřeno je sestavení, ne herní chování.

## Mafiánský příběh a kamerové scény — 6. října 2026

- Po opuštění hlavního menu **F8** zahájí nebo nabídne další dostupnou hlavní zakázku. První nový prolog vede k restauraci Marino; **M** zobrazí mapu s cílem. Probíhající mise se nepřepisuje a uložené dokončené mise zůstávají zachované.
- `mafia_story.json` přidává čtyři kapitoly, 21 úkolových kroků a osm dialogových scén. Prolog vysvětluje dluh po otci; další kapitoly řeší výpalné, účetní knihu a zradu Vinnieho, poslední konfrontaci s Donem. Kapitoly se prokládají s původními misemi: druhá po m2, třetí po m5, epilog po m8. Prolog lze zahájit i ve starším uloženém postupu.
- Dialogy původních misí i nové scény používají kameru kolem současné postavy, tři střídající se úhly, kontrolu kolize kamery, filmové pruhy a titulky. Scéna nepřemístí hráče na jiné místo; související úkol ho nejdřív dovede na místo schůzky. **Esc** (gamepad pravé tlačítko) přeskočí rozhovor, ne ostatní úkoly mise.
- Na konci scény, po selhání mise či smrti se vrací kamera a vstup postavy. Dialogy nejsou plánované jako zbylé časovače, které by po přeskočení pokračovaly přes další úkol.
- Jde o kamerové dialogové scény v existujícím prostředí, nikoli nově natočené filmy: nevznikly nové modely Dona/Vinnieho/Sary, herecké nebo obličejové animace ani český dabing. Původní krátké hlasové ruchy zůstávají. Okolní svět není pozastavený a scény nepřidávají nezranitelnost.
- Úplné sestavení modulu uspělo (38 kroků, 6. října 2026). Nasazené zdrojové soubory a příběhová data se shodují s upravenými kopiemi. Herní spuštění, vzhled scén a návrat ovládání nejsou na přání uživatele testované.

## Otevřené silnice a fronty aut — 6. října 2026

Sestavení poslední úpravy provozu uspělo (4 kroky). Hra se na přání uživatele nespouštěla; skutečný průjezd mostem není ověřený.

- V datech je 96 otevřených pruhů a dvě uzavřené trasy. Otevřené pruhy už nespojují poslední auto s prvním jako kruhovou frontu. Stejné rozlišení používají odstupy a vzdálenosti k semaforům.
- Na konci otevřeného pruhu auto zpomaluje a zastaví. Návrat na začátek je povolen pouze tehdy, když jsou oba konce více než 150 metrů od všech hráčských pozorovatelů; nejde o ověření skutečné viditelnosti kamerou. U blízkého hráče se tak auto nepřemístí z konce na začátek.
- Při souběžném sbíhání tras auta zohledňují vozidlo před sebou i na jiném pruhu a omezují rychlost podle volného odstupu.
- Nejde o nové propojení konců silnic, navigaci objížděk ani řízení střídavého provozu na jednopruhovém mostě. Pokud auta stojí na konkrétním mostě dál, je potřeba jeho poloha nebo snímek pro opravu konkrétní geometrie či tras.

## Provoz, život interiérů a pěší hlídky — 5. října 2026

Sestavení provozu (12 kroků), úplného modulu po rozšíření populace (38 kroků) i závěrečné opravy brzdy a svědků (5 kroků) uspěla. To potvrzuje překlad a propojení, nikoli spuštění nebo herní funkčnost.

- Brzdění dopravních aut kontroluje trasu po úsecích pruhu včetně zatáček a výšky nájezdu. Blízká auta se přizpůsobují skutečnému povrchu silnice do 1,5 m od exportované výšky, nikoli libovolnému povrchu jiného patra mostu. Silniční síť i známý most používají povrchovou kolizi místo plného konvexního obalu.
- Auta dodržují rychlost pruhu, zpomalují před zatáčkami, udržují odstup, zastavují před překážkami a respektují červenou/oranžovou. Ani na zelenou nemají vjíždět do křižovatky bez volného místa za ní. Přednost zprava se vyhodnocuje u skutečně křížících se tras, ne pouze u průsečíku jejich tečen na zatáčce či mostě. Při delším vzájemném čekání je rozhodnutí deterministické. Auto stojící před červenou neblokuje další směr jako přijíždějící vozidlo.
- Provoz stále používá existující pruhy; není to nový navigační systém libovolného objíždění celého města. Konkrétní most bez snímku a herního ověření nelze označit za potvrzeně opravený.
- Podniky dostávají nejvýše tři místní obyvatele z původních modelů davu: stojící obsluhu a pomalu chodící zákazníky. Umístění i krátké trasy se kontrolují proti statické geometrii. Pokud bezpečné místo není k dispozici, NPC se do stěny nevloží. Používají existující animace a hlasové ruchy, promluvy se zobrazují nad postavou. Míření/střelba vyvolává reakce strachu; zásahy a smrt fungují přes původní systém davu. Nejde o plnou simulaci práce, jídla, sezení či denního rozvrhu.
- Blízké místní NPC mají kolizní kapsle. Po smrti se kapsle vypnou, aby nevznikly neviditelné překážky. Animované postavy sdílejí dosavadní omezený fond, nikoli desítky nových těžkých Actorů.
- Na chodnících se objevují nejvýše čtyři běžné pěší policejní hlídky. Přicházejí postupně, sledují úseky chodníků, při zahlédnutí hledaného hráče se připojí k zásahu a po skončení hledanosti se mohou vrátit k hlídce. Vzdálené neaktivní hlídky se odstraňují. Nejde o nové jízdní policejní patroly.
- Po vystoupení se z lokálního ovládání odstraní zbylé kontexty vozidel a obnoví vazba F i její mapování. Nastupování do aut měří vzdálenost k jejich karoserii, ne jen ke středu; vraky nejsou vydávány za volná pojízdná auta. Stejná obnova se používá i po jízdě v lodi či jako spolujezdec.
- Při převzetí auta se resetuje brzda, plyn, řízení a ruční brzda. Svědci se vybírají podle vzdálenosti se zachováním nejvýše osmi dotazů na dohled; nezávisí na pořadí pouličních a interiérových NPC v seznamu.
- Hra se na přání uživatele nespouští. Překlad není potvrzení správných kolizí, animací, FPS nebo všech situací ve hře. Dřívější blokace modulu ve Windows není touto úpravou řešená.

## Lodě, letadla, ponorka a voda — 5. října 2026

Tyto změny jsou ve skutečném projektu na ploše. Neproběhlo spuštění ani herní test; průjezd konkrétním mostem a vzhled podvodního světa nejsou potvrzené.

Kompletní sestavení (38 kroků) i závěrečné sestavení kolizí původních Blueprintů (4 kroky) uspělo. Ověřený je překlad a propojení modulu, nikoli funkčnost za běhu.

- Ovládání lodi/letadla/ponorky se při vystoupení odstraní. Pohyb postavy, kolize, kamera a herní vstup se obnovují i po opakovaném střídání dopravních prostředků.
- F nastupuje/vystupuje. Výstup hledá volné místo mimo rozměry trupu, na břehu, ve vodě nebo vedle letadla. Pokud žádné bezpečné místo nenajde, nevloží postavu do překážky.
- W/S pohon, A/D zatáčení. Ponorka: Ctrl dolů, Space nahoru. Letadlo: W přidává výkon, S ubírá, Space zvedá příď, Ctrl sklání, A/D náklon, Q/E směrovka. Vrtulník: Space stoupá, Ctrl klesá, W/S dopředu/dozadu, A/D otáčení, Q/E stranou.
- Ponorka hledá skutečné dno, nikoli hladinu používanou pro přistání letadel. U hladiny i dna má hloubkový limit.
- Plavec: Ctrl potápění, Space vyplouvání, pohled dolů + W také klesá. Pod vodou už automatický vztlak neruší potápění; po vystoupení z vozidla se obnoví režim plavání. Původní limit kyslíku zůstává 40 sekund, pak nastává tonutí.
- Oceánové vizuální modely nemají pevnou kolizní podlahu. Auta zachovají gravitaci, voda tlumí rychlost a při ponoření vypne tah motoru. Není to simulace postupného zaplavování kabiny.
- Nástupní vzdálenost se počítá k orientovaným rozměrům trupu i při chybějícím výsledku fyzikálního dotazu.
- Lodě, ponorka a letadla při nízkých FPS integrují pohyb v menších krocích namísto zahazování času každého snímku. Velké záseky nad 0,25 sekundy zůstávají omezené kvůli stabilitě.
- Známý model bascule mostu používá kolizi skutečného povrchu, ne plný konvexní obal. Geometrie mostu ani jeho pohyblivé otevírání nebyly touto změnou přemodelované.
- Nové podvodní lokace ani modely touto opravou nevznikly. Zpřístupňuje se potápění v již existujícím prostředí. Dřívější blokace spouštění Windows zůstává nevyřešená.

## Nejnovější služby, interiéry a příběh — 5. října 2026

Nový systém je v herním modulu, nikoli v testovací mapě. Hra se na přání uživatele nespouštěla. Dřívější blokace spuštění modulu ve Windows nebyla touto změnou řešena. Bez prohlédnutí konkrétního modelu nelze potvrdit odstranění všech geometrických děr ani všech chyb textur.

Kompletní sestavení všech 38 kroků i závěrečné sestavení úpravy vstupů (4 kroky) uspělo. To potvrzuje překlad a propojení modulu, nikoli jeho spuštění nebo správný vzhled ve hře.

- Používají se již přítomné modely interiérů z `placements.json`: obchod, zbraně, oblečení, holič, banka, kasino, bar, klub, restaurace a byt. Nejde o nově ručně vymodelované budovy.
- Označené vstupy: **E** vstoupit / vystoupit. Pokud fyzický vchod dovoluje průchod, lze vejít i chůzí. Vstup přes E přemístí postavu do bezpečného bodu uvnitř stejného interiéru; není to animace otevírání dveřního křídla.
- U pultu **E** otevře služby a nákup. Šipky vybírají, Enter nakupuje, Backspace zavírá. Pravé tlačítko myši zavře obchod a zahájí míření. Nákup zbraní server ověřuje podle skutečné vzdálenosti od prodejny.
- Přepadení: miř nabitou pistolí, puškou nebo brokovnicí na oblast pultu, stiskni **E** a udrž míření 6 sekund. Odchod, smrt nebo spuštění zbraně déle než 0,75 s přepadení ruší. Peníze se vyplatí až na konci; pokladna je pak uzamčená 5 minut. Obchod vyvolá nejméně 2 hvězdy, banka 3. Prodavač reaguje titulkem, nejde o nový kompletní animovaný NPC systém.
- Servisní prostor a označení Neon Coast Customs navazují na původní tuningové menu. Do servisu se přijíždí autem.
- Obchodní vstupy a názvy jsou přidané na mapu. Vstup kontroluje kolize a nesmí vypnout kolizi postavy ani ji vložit do překážky.
- Materiálová oprava postupuje po malých dávkách: obnoví správný výchozí materiál nebo materiál podle názvu slotu. U neurčeného chybějícího městského materiálu je náhradou texturovaný beton; nejde o obnovení původní ztracené textury. Správné materiály ani záměrná průhlednost se nepřepisují. U MetaHumanů se nepoužívá betonová náhrada.
- Transparentní městské modely mají vypnuté Nanite na komponentě. Interiéry dostávají podpůrnou texturovanou podlahu a kolizi podle povrchu místo plného blokujícího obalu. Neopravuje to chybějící polygony stěn, těla či oblečení.
- `Content/NeonCoast/Data/city_story.json` přidává 3 kapitoly a 16 kroků: poznání služeb, následky přepadení a dohru po válce rodin. První kapitola je dostupná u původního zadavatele; další vyžadují původní mise m2 a m8. Původní mise a uložené dokončené kapitoly zůstávají zachované.
- Nové cíle se vyhodnocují podle skutečného vstupu nebo dokončeného přepadení; neznámý cíl nesmí automaticky dokončit misi.

## Nejnovější optimalizace a hledanost — 5. října 2026

Nasazené přímo do tohoto projektu. Na přání uživatele se hra pro tyto změny NEspouštěla a NEprováděl se herní test ani měření FPS. Starší úspěšné testy níže se netýkají této sestavy. Poslední známá blokace modulu ve Windows (4551) není touto optimalizací vyřešena; ochrana systému nebyla změněna.

- Provoz poblíž hráče se počítá 30krát za sekundu, vzdálená auta 5krát za sekundu. Jízda hráče a fyzika ovládaného auta se tímto neomezují.
- Přednost na křižovatkách používá prostorovou mřížku okolních aut místo porovnávání každého auta s celým provozem. Fronta na pruhu kontroluje nejbližšího následníka ze seřazeného seznamu.
- Auta a chodci obnovují vykreslovací skupiny pouze při změně. Chodci mají pohybový řídicí tick 30 Hz; jejich vlastní kosterní animace zůstávají samostatné.
- Výchozí konfigurace a zástupce mají lehčí stíny, osvětlení, odrazy, efekty a vegetaci, vypnutý motion blur a limit 60 FPS. Kvalita textur zůstává 3, rozlišení vykreslování 70 %.
- Policie při ztrátě dohledu pátrá na posledním známém místě, ne podle aktuální skryté polohy hráče. Týká se aut, policistů na zemi i vrtulníku. Hvězdy při úniku ubývají postupně.
- Pozorování vyžaduje průhled bez překážky. Svědci mezi chodci mají omezený počet dohledových dotazů; opakované výstřely nemohou okamžitě navršit hvězdy.
- Posily mají časové rozestupy a početní rozpočty; pátrající jednotky jsou přiřazeny ke správnému hráči. Vzdálené policejní vraky se odstraňují i během pronásledování.
- HUD rozlišuje PRONASLEDOVANI a PATRANI. Zatčení vyžaduje dohled; při ztrátě dohledu se jeho čas ruší.
- Původní konfigurace a zástupce jsou zachované v `Saved/RepairBackup_20261005_Performance`.

Úpravy snižují počet výpočtů a grafickou zátěž. Bez měření nelze potvrdit konkrétní zvýšení FPS ani zaručit hru bez záseků.

## Předchozí ověřená sestava 5. října 2026

Nové zdrojové opravy: opětovná vazba F na novou vstupní komponentu při vystoupení, obnova vstupu postavy, oprava nechráněných statických ukazatelů na materiály efektů střelby, synchronizace přípravy kostry vozidla s vykreslováním, střelba do fyzických dopravních těles, náhradní animovaný pád chodce bez ragdollu, obnovení materiálů chodců, průběžná detekce přejetí a ignorování povrchu stoupajícího mostu při brzdění provozu.

Nová minimapa a velká mapa: M otevřít/zavřít, Esc zavřít, kolečko nebo +/- přiblížení, I/J/K/L posun, Home vystředit na hráče. Ve velké mapě: 1 nejbližší servis, 2 benzinka, 3 zbraně, 4 policie, 5 nemocnice. Značky a seznam vzdáleností zahrnují servis, obchody a mise, oblečení, benzinky, nemocnici a policii. Podrobné názvy se zobrazují až po přiblížení; seznam míst neopakuje stejné názvy.

Po kompletním sestavení modulu prošel vykreslovaný test `Saved/Logs/GameplayRepairRebuild_20261005.log`: 36 PASS, 0 FAIL, DONE. 160 střel zasáhlo 20 typů aut s ověřeným poškozením; efekty přežily explicitní uvolnění paměti. Ověřené jsou materiály chodce, pád, chůze po dvou vystoupeních, načtení značek mapy i předchozí kontroly jízdy, střechy a překážky. Snímek velké mapy byl vizuálně prohlédnut.

Finální test po úpravách čitelnosti mapy a vykreslování skel: `Saved/Logs/GameplayRepairVerified_20261005.log`, 37 PASS, 0 FAIL, DONE. Navíc ověřen skutečný zásah do fyzického dopravního tělesa a jeho převod na poškozené ovladatelné auto. Finální snímek mapy: `Saved/Screenshots/WindowsEditor/HighresScreenshot00011.png`. Kompilace této sestavy uspěla.

Předchozí neúspěšné pokusy jsou v logách GameplayRepair_20261005 a GameplayRepairFinal_20261005; nejsou výsledkem aktuální kompletní sestavy. Bezpečnostní nastavení Windows se neměnila a ochrana se neobchází. Oprava konkrétního provozu na uživatelově mostě a všechny vzhledy chodců nebyly ručně ověřené na celé mapě.

Tato složka obsahuje skutečné soubory Unreal projektu, nikoli jen zástupce.
Projekt: `NeonCoastGame.uproject`. Engine: `C:\UE_5.8`.

Původní projekt `C:\NeonCoastGame` byl zachován. Nejnovější opravy jsou nyní nasazované do této kopie na ploše; stará kopie již neobsahuje všechny nové změny.

## Stav nové animační verze 5. října 2026

POZOR: Nový modul se přeložil, ale jeho spuštění dvakrát zablokoval Windows Smart App Control (chyba 4551, událost CodeIntegrity 3077). Tato nová verze se proto zatím nespustí. Ochrana Windows nebyla změněna. Dřívější úspěšné testy uvedené níže nejsou ověřením této nové verze.

- Přidaný nativní animační systém: míření, skok, pád, přistání, přebíjení, dřep a procedurální plavání/potápění.
- Pohyby se přenášejí podle společných kostí se zachováním délek cílové kostry. Nejde o přímo přehrávané nekompatibilní klipy na MetaHumanovi.
- Zbraň se váže na ruku viditelného těla místo skrytého pomocného modelu.
- Kompilace prošla. Vzhled a funkčnost nových animací zatím NEJSOU ověřené ve spuštěné hře.
- Rozšířený test se spouští parametry `-NCSelfTest -NCAnimationTest -NCVisualTest`; voda se v animační části simuluje, nenahrazuje test plavání ve skutečné vodě.
- Záznamy blokace: `Saved/Logs/AnimationRepair_20261005.log` a `Saved/Logs/AnimationRepairRetry_20261005.log`.
- K dokončení je potřeba důvěryhodné podepsání herního modulu v souladu s bezpečnostní politikou Windows.

## Nové změny 4. října 2026

- Semaforové fáze: 20 sekund zelená, 3 sekundy oranžová, 2 sekundy společná červená. Protichůdné přístupy se střídají.
- Stopčáry se odvozují z umístění a orientace exportovaných semaforů a přiřazují k odpovídajícím jízdním pruhům.
- Dopravní auta brzdí před červenou. Na oranžovou projedou jen tehdy, když už nelze bezpečně zastavit.
- Křižující se provoz má přednost zprava, kontrolu obsazeného průjezdu a rozlišení mimoúrovňových křížení. Dlouhé vzájemné čekání má deterministické řešení.
- Vystupování spolujezdce hledá volné místo na zemi a znovu aktivuje ovládání a pohyb; nevkládá postavu do překážky.
- Předchozí opravy materiálů, podvozku, kolizních těles, chůze po vystoupení a natočení postavy jsou součástí této kopie.
- Nový tyrkysový stylizovaný kabriolet má otevřenou kabinu a animovanou skládací střechu. Objeví se poblíž hráče na volném rovném místě. R střechu ovládá při rychlosti do 10 km/h; u převráceného auta zůstává R resetem.
- Automatický test kontroluje materiály, pohon, brzdění provozu před překážkou, kolizi auta, návrat ovládání po vystoupení a dokončení animace střechy. Selhání vrací nenulový stav.
- Kamera aut se při startu nastavuje nad a za podvozek, externí kamera je aktivní a vnitřní vypnutá. Opraveno umístění pod autem způsobené starými výchozími hodnotami Blueprintu.
- Kolize a brzdění provozu se aktivují také podle polohy ovládané postavy/auta, nejen kamery. To řeší změny kamer i běh bez lokálního vykreslování.

Nová kopie se 4. října 2026 úspěšně spustila a dokončila automatický herní test bez původní chyby DLL. Bezpečnostní nastavení Windows se neměnila.

Výsledek opakovaného testu `Saved/Logs/DesktopRepairSelfTest2.log`: 10 PASS, 0 FAIL, DONE. Potvrzené kontroly: nekolizní zelené fáze semaforů, zastavení provozu před překážkou, blokující kolize provozu s vozidlem, přiřazení materiálů sedanu, pohon sedanu, odebrání automobilového ovládání, chůze po vystoupení, příkaz zavření střechy, dokončení animace a návrat ovládání po vystoupení z kabrioletu.

Poslední vykreslovaný test `Saved/Logs/DesktopRepairVisualTest3.log`: 11 PASS, 0 FAIL, DONE. Navíc ověřena výška a odstup externí kamery. Snímky `Saved/Screenshots/WindowsEditor/HighresScreenshot00004.png` a `HighresScreenshot00005.png` byly vizuálně prohlédnuty: sedan i tyrkysový kabriolet se vykreslují s materiály a kamerou za vozidlem.

Jízda sedanu se ověřovala na pomocné rovné plošině, nikoli na všech ulicích. Tyto testy nejsou důkazem správného vzhledu všech textur, plynulosti celé mapy, všech kolizí, správného natočení všech animací ani úplnosti pravidel silničního provozu. Plavání, potápění a policejní souboje tento test neověřuje.

Kompilace nových změn úspěšně dokončena 4. října 2026. Nejde o hotovou samostatně zabalenou hru bez Unreal Enginu. Kabriolet je jednoduchý nový modulární model, nikoli fotorealistická úprava původního sportovního auta.

Zálohy starších změn: `Saved\RepairBackup_20260930`.
