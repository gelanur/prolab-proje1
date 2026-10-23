#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
 
#define MAX_GOREV      5000
#define MAX_SATIR      512
#define MAX_TUR        40
#define TEKNISYEN_SAY  4
#define KONUM_SAY      5
#define SAYFA_BOYU     20
 
#ifndef GECIS_SURESI
#define GECIS_SURESI   1
#endif
 
typedef enum { HENUZ_GELMEDI = 0, BEKLIYOR, DEVAM_EDIYOR, TAMAMLANDI } Durum;
typedef enum { ALG_YOK = 0, ALG_SJ, ALG_PRIORITY, ALG_RR, ALG_KONUM } Algoritma;
typedef enum { T_BOS = 0, T_GECIS, T_CALISIYOR } TeknisyenDurum;
 
typedef struct {
int  id;
int  gelis;
int  sure;
int  oncelik;
char tur[MAX_TUR];
int  konum;
 
Durum durum;
int  kalan;
int  teknisyen;
int  ilk_baslama;
int  bitis;
int  bitiren;
} Gorev;
 
typedef struct {
TeknisyenDurum durum;
int gorev;
int konum;
int gecis_kalan;
int dilim;
int calisilan;
int gecis_sayisi;
} Teknisyen;
 
static const char *KONUMLAR[KONUM_SAY] = {
"LAB_A", "LAB_B", "DERSLIK_1", "DERSLIK_2", "DERSLIK_3"
};
static const char *DURUM_ADLARI[4] = {
"Henuz gelmedi", "Bekliyor", "Devam ediyor", "Tamamlandi"
};
static const char *ALG_ADLARI[5] = {
"Secilmedi", "Shortest Job", "Priority", "Round Robin", "Konum Sadakatli (Kendi)"
};
 
static Gorev     gorevler[MAX_GOREV];
static int       gorev_sayisi = 0;
static Teknisyen tek[TEKNISYEN_SAY];
static int       saat = 0;
static Algoritma algoritma = ALG_YOK;
static int       quantum = 0;
 
static int kuyruk[MAX_GOREV];
static int k_bas = 0, k_say = 0;
 
static void kuyruk_temizle(void) { k_bas = 0; k_say = 0; }
 
static void kuyruk_ekle(int idx)
{
kuyruk[(k_bas + k_say) % MAX_GOREV] = idx;
k_say++;
}
 
static int kuyruk_cikar(void)
{
    int idx = kuyruk[k_bas];
    k_bas = (k_bas + 1) % MAX_GOREV;
    k_say--;
    return idx;
}
 
static void satir_oku(char *buf, int boyut)
{
    size_t n;
    if (fgets(buf, boyut, stdin) == NULL) {
        printf("\nGirdi sona erdi, program kapatiliyor.\n");
        exit(0);
    }
    n = strlen(buf);
    if (n > 0 && buf[n - 1] == '\n') {
        buf[n - 1] = '\0';
        if (n > 1 && buf[n - 2] == '\r') buf[n - 2] = '\0';
    } else {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) { }
    }
}
 
static int tamsayi_cevir(const char *s, int *sonuc)
{
    char *son;
    long v;
    if (s == NULL) return 0;
    errno = 0;
    v = strtol(s, &son, 10);
    if (son == s) return 0;
    while (isspace((unsigned char)*son)) son++;
    if (*son != '\0') return 0;
    if (errno == ERANGE || v < INT_MIN || v > INT_MAX) return 0;
    *sonuc = (int)v;
    return 1;
}
 
static int tamsayi_al(const char *mesaj, int alt, int ust)
{
    char buf[128];
    int v;
    for (;;) {
        printf("%s", mesaj);
        satir_oku(buf, sizeof buf);
        if (tamsayi_cevir(buf, &v) && v >= alt && v <= ust) return v;
        if (ust == INT_MAX)
            printf("Gecersiz giris. %d veya daha buyuk bir tam sayi giriniz.\n", alt);
        else
            printf("Gecersiz giris. %d ile %d arasinda bir tam sayi giriniz.\n", alt, ust);
    }
}
 
static void devam_icin_bekle(void)
{
    char buf[16];
    printf("\nDevam etmek icin Enter'a basiniz...");
    satir_oku(buf, sizeof buf);
}
 
static int konum_bul(const char *s)
{
    int i;
    for (i = 0; i < KONUM_SAY; i++)
        if (strcmp(s, KONUMLAR[i]) == 0) return i;
    return -1;
}
static int id_karsilastir(const void *a, const void *b)
{
    const Gorev *x = (const Gorev *)a, *y = (const Gorev *)b;
    return (x->id > y->id) - (x->id < y->id);
}
 
static int id_ile_bul(int id)
{
    int sol = 0, sag = gorev_sayisi - 1;
    while (sol <= sag) {
        int orta = sol + (sag - sol) / 2;
        if (gorevler[orta].id == id) return orta;
        if (gorevler[orta].id < id) sol = orta + 1;
        else sag = orta - 1;
    }
    return -1;
}
 
static int dosya_oku(const char *yol)
{
    FILE *fp;
    char satir[MAX_SATIR];
    int satir_no = 0, reddedilen = 0, i;
 
    fp = fopen(yol, "r");
    if (fp == NULL) {
        printf("HATA: '%s' dosyasi acilamadi.\n", yol);
        return 0;
    }
 
    gorev_sayisi = 0;
    while (fgets(satir, sizeof satir, fp) != NULL) {
        char *p, *alan[7], *tok;
        int alan_say = 0;
        int id, gelis, sure, oncelik, konum;
        int n = (int)strlen(satir);
 
        satir_no++;
 
        
        if (n > 0 && satir[n - 1] != '\n') {
            int c = fgetc(fp);
            if (c != EOF) {
                while (c != '\n' && c != EOF) c = fgetc(fp);
                printf("Satir %d reddedildi: satir cok uzun.\n", satir_no);
                reddedilen++;
                continue;
            }
        }
 
        p = satir;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '#') continue;
 
        tok = strtok(p, " \t\r\n\v\f");
        while (tok != NULL && alan_say < 7) {
            alan[alan_say++] = tok;
            tok = strtok(NULL, " \t\r\n\v\f");
        }
        if (alan_say < 6) {
            printf("Satir %d reddedildi: eksik alan (%d alan var, 6 olmali).\n",satir_no, alan_say);
            reddedilen++; continue;
        }
        if (alan_say > 6) {
            printf("Satir %d reddedildi: fazla alan (6 alan olmali).\n", satir_no);
            reddedilen++; continue;
        }
 
        if (!tamsayi_cevir(alan[0], &id) || id <= 0) {
            printf("Satir %d reddedildi: gecersiz ID '%s' (pozitif tam sayi olmali).\n",satir_no, alan[0]);
            reddedilen++; continue;
        }
        if (!tamsayi_cevir(alan[1], &gelis) || gelis < 0) {
            printf("Satir %d reddedildi: gecersiz gelis zamani '%s'.\n", satir_no, alan[1]);
            reddedilen++; continue;
        }
        if (!tamsayi_cevir(alan[2], &sure) || sure <= 0) {
            printf("Satir %d reddedildi: gecersiz sure '%s' (sifirdan buyuk olmali).\n",satir_no, alan[2]);
            reddedilen++; continue;
        }
        if (!tamsayi_cevir(alan[3], &oncelik) || oncelik < 1 || oncelik > 5) {
            printf("Satir %d reddedildi: gecersiz oncelik '%s' (1-5 olmali).\n",satir_no, alan[3]);
            reddedilen++; continue;
        }
        if (strlen(alan[4]) > MAX_TUR - 1) {
            printf("Satir %d reddedildi: gorev turu %d karakterden uzun.\n",satir_no, MAX_TUR - 1);
            reddedilen++; continue;
        }
        konum = konum_bul(alan[5]);
        if (konum < 0) {
            printf("Satir %d reddedildi: gecersiz konum '%s'.\n", satir_no, alan[5]);
            reddedilen++; continue;
        }
 
        for (i = 0; i < gorev_sayisi; i++)
            if (gorevler[i].id == id) break;
        if (i < gorev_sayisi) {
            printf("Satir %d reddedildi: yinelenen ID %d (ilk kayit korundu).\n",satir_no, id);
            reddedilen++; continue;
        }
        if (gorev_sayisi >= MAX_GOREV) {
            printf("Satir %d reddedildi: kapasite asildi (en fazla %d gorev).\n", satir_no, MAX_GOREV);
            reddedilen++; continue;
        }
 
        gorevler[gorev_sayisi].id = id;
        gorevler[gorev_sayisi].gelis = gelis;
        gorevler[gorev_sayisi].sure = sure;
        gorevler[gorev_sayisi].oncelik = oncelik;
        strcpy(gorevler[gorev_sayisi].tur, alan[4]);
        gorevler[gorev_sayisi].konum = konum;
        gorev_sayisi++;
    }
    if (ferror(fp))
        printf("UYARI: Dosya okunurken bir hata olustu.\n");
    fclose(fp);
 
    qsort(gorevler, gorev_sayisi, sizeof(Gorev), id_karsilastir);
    printf("\nDosya okundu: %d gecerli gorev, %d reddedilen satir.\n",gorev_sayisi, reddedilen);
    if (gorev_sayisi == 0)
        printf("UYARI: Dosyada gecerli gorev yok (bos dosya).\n");
    return 1;
}
static int daha_oncelikli(const Gorev *a, const Gorev *b)
{
    if (algoritma == ALG_SJ) {
        if (a->sure != b->sure) return a->sure < b->sure;
    } else {
        if (a->oncelik != b->oncelik) return a->oncelik < b->oncelik;
    }
    if (a->gelis != b->gelis) return a->gelis < b->gelis;
    return a->id < b->id;
}
static int bekleyen_sec(void)
{
    int i, en = -1;
    for (i = 0; i < gorev_sayisi; i++) {
        if (gorevler[i].durum != BEKLIYOR) continue;
        if (en == -1 || daha_oncelikli(&gorevler[i], &gorevler[en])) en = i;
    }
    return en;
}
/* ---- 4 - Kendi Algoritmamiz: Konum Sadakatli En Uzun Once ---------------
 * Hedef: makespan'i (son gorevin bitis zamani) dusurmek. Iki fikir birlikte:
 *  - Teknisyen mumkunse bulundugu konumdan ayrilmaz (gecis maliyeti yok).
 *  - Uzun gorevler erken baslatilir; sonda tek uzun gorev kalip bitisi
 *    uzatmasin diye (en uzun once / LPT mantigi).
 * Teknisyen bos kalmaz: gelmis bekleyen gorev varsa mutlaka birini alir.
 * Gorevler kesilmez (RR gibi bolunmez). */
 
/* a, b'den once secilmeli mi? Once uzun sure, sonra erken gelis, sonra kucuk ID. */
static int daha_uzun(const Gorev *a, const Gorev *b)
{
    if (a->sure != b->sure) return a->sure > b->sure;
    if (a->gelis != b->gelis) return a->gelis < b->gelis;
    return a->id < b->id;
}
 
/* t. teknisyen icin gorev secer; bekleyen gorev yoksa -1 dondurur. */
static int konum_sec(int t)
{
    int i, k, u, en = -1, en_konum = -1, yerel = -1, kritik;
    long long kalan_toplam = 0;
    long long konum_yuk[KONUM_SAY] = {0, 0, 0, 0, 0};   /* konumdaki bekleyen is */
    int konum_tek[KONUM_SAY] = {0, 0, 0, 0, 0};          /* konumdaki teknisyen   */
 
    /* Tum gelmis/bitmemis islerin kalan toplami; konum yukleri; en uzun bekleyen */
    for (i = 0; i < gorev_sayisi; i++) {
        const Gorev *g = &gorevler[i];
        if (g->durum == DEVAM_EDIYOR) {
            kalan_toplam += g->kalan;
        } else if (g->durum == BEKLIYOR) {
            kalan_toplam += g->kalan;
            konum_yuk[g->konum] += g->sure;
            if (en == -1 || daha_uzun(g, &gorevler[en])) en = i;
        }
    }
    if (en == -1) return -1;
 
    /* Kural 0: Kritik gorev. Bekleyen en uzun gorev, teknisyen basina dusen
     * ortalama kalan isten uzunsa bitisi o belirler; konumuna bakmadan basla. */
    kritik = (int)((kalan_toplam + TEKNISYEN_SAY - 1) / TEKNISYEN_SAY);
    if (gorevler[en].sure >= kritik) return en;
 
    /* Kural 1: Bulundugun konumda bekleyen gorev varsa onlardan en uzununu al */
    if (tek[t].konum != -1) {
        for (i = 0; i < gorev_sayisi; i++) {
            if (gorevler[i].durum != BEKLIYOR) continue;
            if (gorevler[i].konum != tek[t].konum) continue;
            if (yerel == -1 || daha_uzun(&gorevler[i], &gorevler[yerel])) yerel = i;
        }
        if (yerel != -1) return yerel;
    }
 
    /* Kural 2: Gitmek zorundaysan puani en yuksek konuma git.
     * puan = konumdaki bekleyen is / (oradaki veya oraya giden teknisyen + 1) */
    for (u = 0; u < TEKNISYEN_SAY; u++)
        if (u != t && tek[u].durum != T_BOS && tek[u].konum >= 0)
            konum_tek[tek[u].konum]++;
    for (k = 0; k < KONUM_SAY; k++) {
        if (konum_yuk[k] == 0) continue;
        /* kesirleri capraz carparak karsilastir (ondalik kullanmadan) */
        if (en_konum == -1 ||
            konum_yuk[k] * (konum_tek[en_konum] + 1) >
            konum_yuk[en_konum] * (konum_tek[k] + 1))
            en_konum = k;
    }
    en = -1;
    for (i = 0; i < gorev_sayisi; i++) {
        if (gorevler[i].durum != BEKLIYOR || gorevler[i].konum != en_konum) continue;
        if (en == -1 || daha_uzun(&gorevler[i], &gorevler[en])) en = i;
    }
    return en;
}
 
static void gorev_ata(int t, int g)
{
    Teknisyen *tk = &tek[t];
    Gorev *gv = &gorevler[g];
 
    gv->durum = DEVAM_EDIYOR;
    gv->teknisyen = t;
    tk->gorev = g;
    tk->dilim = 0;
 
    if (tk->konum != -1 && tk->konum != gv->konum && GECIS_SURESI > 0) {
        tk->durum = T_GECIS;
        tk->gecis_kalan = GECIS_SURESI;
        tk->gecis_sayisi++;
    } else {
        tk->durum = T_CALISIYOR;
        if (gv->ilk_baslama == -1) gv->ilk_baslama = saat;
    }
    tk->konum = gv->konum;
}
static void atamalari_yap(void)
{
    int t;
    for (t = 0; t < TEKNISYEN_SAY; t++) {
        int g;
        if (tek[t].durum != T_BOS) continue;
        if (algoritma == ALG_RR)
            g = (k_say > 0) ? kuyruk_cikar() : -1;
        else if (algoritma == ALG_KONUM)
            g = konum_sec(t);
        else
            g = bekleyen_sec();
        if (g == -1) break;
        gorev_ata(t, g);
    }
}
 
static void gelenleri_isle(void)
{
    int i;
    for (i = 0; i < gorev_sayisi; i++) {
        if (gorevler[i].durum == HENUZ_GELMEDI && gorevler[i].gelis <= saat) {
            gorevler[i].durum = BEKLIYOR;
            if (algoritma == ALG_RR) kuyruk_ekle(i);
        }
    }
}
 
static void sim_sifirla(void)
{
    int i;
    saat = 0;
    kuyruk_temizle();
    for (i = 0; i < gorev_sayisi; i++) {
        gorevler[i].durum = HENUZ_GELMEDI;
        gorevler[i].kalan = gorevler[i].sure;
        gorevler[i].teknisyen = -1;
        gorevler[i].ilk_baslama = -1;
        gorevler[i].bitis = -1;
        gorevler[i].bitiren = -1;
    }
    for (i = 0; i < TEKNISYEN_SAY; i++) {
        tek[i].durum = T_BOS;
        tek[i].gorev = -1;
        tek[i].konum = -1;
        tek[i].gecis_kalan = 0;
        tek[i].dilim = 0;
        tek[i].calisilan = 0;
        tek[i].gecis_sayisi = 0;
    }
    if (algoritma != ALG_YOK) {
        gelenleri_isle();
        atamalari_yap();
    }
}
 
static void bir_adim(void)
{
    int dolan[TEKNISYEN_SAY], dolan_say = 0, t;
 
    for (t = 0; t < TEKNISYEN_SAY; t++) {
        Teknisyen *tk = &tek[t];
        if (tk->durum == T_GECIS) {
            tk->gecis_kalan--;
            if (tk->gecis_kalan == 0) {
                Gorev *g = &gorevler[tk->gorev];
                tk->durum = T_CALISIYOR;
                if (g->ilk_baslama == -1) g->ilk_baslama = saat + 1;
            }
        } else if (tk->durum == T_CALISIYOR) {
            gorevler[tk->gorev].kalan--;
            tk->calisilan++;
            tk->dilim++;
        }
    }
    saat++;
 
    for (t = 0; t < TEKNISYEN_SAY; t++) {
        Teknisyen *tk = &tek[t];
        Gorev *g;
        if (tk->durum != T_CALISIYOR) continue;
        g = &gorevler[tk->gorev];
 
        if (g->kalan == 0) {
            g->durum = TAMAMLANDI;
            g->bitis = saat;
            g->bitiren = t;
            g->teknisyen = -1;
            tk->durum = T_BOS;
            tk->gorev = -1;
            tk->dilim = 0;
        } else if (algoritma == ALG_RR && tk->dilim >= quantum) {
            g->durum = BEKLIYOR;
            g->teknisyen = -1;
            dolan[dolan_say++] = tk->gorev;
            tk->durum = T_BOS;
            tk->gorev = -1;
            tk->dilim = 0;
        }
    }
    gelenleri_isle();
    for (t = 0; t < dolan_say; t++) kuyruk_ekle(dolan[t]);
    atamalari_yap();
}
 
static void zamana_git(int hedef)
{
    if (hedef < saat) sim_sifirla();
    while (saat < hedef) bir_adim();
}
static void tablo_basligi(void)
{
    printf("%-7s %-6s %-5s %-8s %-14s %-10s %-6s\n","ID", "Gelis", "Sure", "Oncelik", "Durum", "Teknisyen", "Kalan");
    printf("--------------------------------------------------------------\n");
}
 
static void gorev_satiri(const Gorev *g)
{
    char tk[16];
    if (g->teknisyen >= 0) sprintf(tk, "T%d", g->teknisyen + 1);
    else strcpy(tk, "-");
    printf("%-7d %-6d %-5d %-8d %-14s %-10s %-6d\n",g->id, g->gelis, g->sure, g->oncelik, DURUM_ADLARI[g->durum], tk, g->kalan);
}
 
static void gorevleri_listele(int filtre)
{
    int i, sayfadaki = 0, toplam = 0;
    char buf[16];
 
    for (i = 0; i < gorev_sayisi; i++) {
        if (filtre != -1 && (int)gorevler[i].durum != filtre) continue;
        if (sayfadaki == 0) { printf("\n"); tablo_basligi(); }
        gorev_satiri(&gorevler[i]);
        sayfadaki++;
        toplam++;
        if (sayfadaki == SAYFA_BOYU) {
            printf("-- Sonraki sayfa icin Enter, cikmak icin q: ");
            satir_oku(buf, sizeof buf);
            if (buf[0] == 'q' || buf[0] == 'Q') return;
            sayfadaki = 0;
        }
    }
    if (toplam == 0) printf("\nListelenecek gorev yok.\n");
    else printf("\nToplam %d gorev listelendi.\n", toplam);
}
 
static void durum_listele(void)
{
    int s;
    printf("\n1 - Henuz gelmedi\n2 - Bekliyor\n3 - Devam ediyor\n4 - Tamamlandi\n");
    s = tamsayi_al("Durum seciniz: ", 1, 4);
    gorevleri_listele(s - 1);
}
 
static void id_sorgula(void)
{
    int id = tamsayi_al("Gorev ID: ", INT_MIN, INT_MAX);
    int i = id_ile_bul(id);
    const Gorev *g;
 
    if (i < 0) { printf("ID %d ile gorev bulunamadi.\n", id); return; }
    g = &gorevler[i];
 
    printf("\n--- Gorev %d ---\n", g->id);
    printf("Gorev turu        : %s\n", g->tur);
    printf("Konum             : %s\n", KONUMLAR[g->konum]);
    printf("Gelis zamani      : %d\n", g->gelis);
    printf("Toplam sure       : %d\n", g->sure);
    printf("Oncelik           : %d\n", g->oncelik);
    printf("Durum             : %s\n", DURUM_ADLARI[g->durum]);
    if (g->teknisyen >= 0)
        printf("Guncel teknisyen  : T%d%s\n", g->teknisyen + 1,tek[g->teknisyen].durum == T_GECIS ? " (teknisyen geciste)" : "");
    else
        printf("Guncel teknisyen  : -\n");
    printf("Kalan sure        : %d\n", g->kalan);
    if (g->ilk_baslama >= 0) printf("Ilk baslama       : %d\n", g->ilk_baslama);
    else                     printf("Ilk baslama       : -\n");
    if (g->durum == TAMAMLANDI) {
        printf("Tamamlanma        : %d\n", g->bitis);
        printf("Bitiren teknisyen : T%d\n", g->bitiren + 1);
        printf("Toplam bekleme    : %d\n", g->bitis - g->gelis - g->sure);
    } else {
        printf("Tamamlanma        : -\n");
    }
}
 
static void teknisyenleri_goster(void)
{
    int t;
    printf("\nZaman: %d\n", saat);
    printf("%-5s %-9s %-7s %-12s %-11s %-10s %-10s %-6s\n","Tek.", "Durum", "Gorev", "GorevKalan",algoritma == ALG_RR ? "DilimKalan" : "", "Calisilan", "Konum", "Gecis");
    printf("---------------------------------------------------------------------------\n");
    for (t = 0; t < TEKNISYEN_SAY; t++) {
        const Teknisyen *tk = &tek[t];
        const char *d = tk->durum == T_BOS ? "Bos" :
                        tk->durum == T_GECIS ? "Geciste" : "Mesgul";
        char gorev[16], kalan[16], dilim[16];
 
        if (tk->gorev >= 0) {
            sprintf(gorev, "%d", gorevler[tk->gorev].id);
            sprintf(kalan, "%d", gorevler[tk->gorev].kalan);
            /* Dilim suresi ile gorevin kalan suresi ayri degerlerdir */
            sprintf(dilim, "%d", quantum - tk->dilim);
        } else {
            strcpy(gorev, "-"); strcpy(kalan, "-"); strcpy(dilim, "-");
        }
        if (algoritma != ALG_RR) dilim[0] = '\0';
 
        printf("T%-4d %-9s %-7s %-12s %-11s %-10d %-10s %-6d\n",t + 1, d, gorev, kalan, dilim, tk->calisilan, tk->konum >= 0 ? KONUMLAR[tk->konum] : "-", tk->gecis_sayisi);
    }
}
 
static void istatistikleri_goster(void)
{
    int sayac[4] = {0, 0, 0, 0};
    long long toplam_bekleme = 0, toplam_sistemde = 0, toplam_calisma = 0;
    int i, t, son_bitis = -1, toplam_gecis = 0;
 
    for (i = 0; i < gorev_sayisi; i++) {
        const Gorev *g = &gorevler[i];
        sayac[g->durum]++;
        if (g->durum == TAMAMLANDI) {
            toplam_bekleme  += g->bitis - g->gelis - g->sure;
            toplam_sistemde += g->bitis - g->gelis;
            if (g->bitis > son_bitis) son_bitis = g->bitis;
        }
    }
 
    printf("\n========== ISTATISTIKLER (zaman %d) ==========\n", saat);
    printf("Algoritma            : %s", ALG_ADLARI[algoritma]);
    if (algoritma == ALG_RR) printf(" (quantum %d)", quantum);
    printf("\nGecerli gorev sayisi : %d\n", gorev_sayisi);
    printf("  Henuz gelmedi      : %d\n", sayac[HENUZ_GELMEDI]);
    printf("  Bekliyor           : %d\n", sayac[BEKLIYOR]);
    printf("  Devam ediyor       : %d\n", sayac[DEVAM_EDIYOR]);
    printf("  Tamamlandi         : %d\n", sayac[TAMAMLANDI]);
    printf("  (Toplam            : %d)\n",sayac[0] + sayac[1] + sayac[2] + sayac[3]);
 
    if (gorev_sayisi > 0 && sayac[TAMAMLANDI] == gorev_sayisi)
        printf("Son bitis zamani     : %d  (Son gorevin bitis zamani - tum gorevler tamamlandi)\n", son_bitis);
    else if (son_bitis >= 0)
        printf("Son bitis zamani     : %d  (henuz tum gorevler tamamlanmadi)\n", son_bitis);
    else
        printf("Son bitis zamani     : -\n");
 
    if (sayac[TAMAMLANDI] > 0) {
        printf("Ortalama bekleme     : %.3f\n",(double)toplam_bekleme / sayac[TAMAMLANDI]);
        printf("Ortalama sistemde kalma: %.3f\n",(double)toplam_sistemde / sayac[TAMAMLANDI]);
    } else {
        printf("Ortalama bekleme     : hesaplanamaz (tamamlanan gorev yok)\n");
        printf("Ortalama sistemde kalma: hesaplanamaz (tamamlanan gorev yok)\n");
    }
 
    printf("\nTeknisyen kullanim oranlari:\n");
    for (t = 0; t < TEKNISYEN_SAY; t++) {
        toplam_calisma += tek[t].calisilan;
        toplam_gecis += tek[t].gecis_sayisi;
        if (saat == 0)
            printf("  T%d: hesaplanamaz (zaman 0)\n", t + 1);
        else
            printf("  T%d: %%%.3f  (%d / %d birim, %d gecis)\n", t + 1,
                   100.0 * tek[t].calisilan / saat, tek[t].calisilan, saat,tek[t].gecis_sayisi);
    }
    if (saat == 0)
        printf("Genel kullanim       : hesaplanamaz (zaman 0)\n");
    else
        printf("Genel kullanim       : %%%.3f\n",
               100.0 * toplam_calisma / (4.0 * saat));
    printf("Toplam konum gecisi  : %d (her biri %d birim)\n", toplam_gecis, GECIS_SURESI);
}
 
static void algoritma_sec(void)
{
    int s;
    printf("\n--- Algoritma Secimi ---\n");
    printf("1 - Shortest Job\n");
    printf("2 - Priority\n");
    printf("3 - Round Robin\n");
    printf("4 - Kendi Algoritmamiz (Konum Sadakatli En Uzun Once)\n");
    s = tamsayi_al("Seciminiz: ", 1, 4);
 
    algoritma = (Algoritma)s;
    quantum = 0;
    if (algoritma == ALG_RR)
        quantum = tamsayi_al("Round Robin zaman dilimi (quantum): ", 1, INT_MAX);
 
    sim_sifirla();
    printf("%s secildi. Simulasyon zaman 0'dan baslatildi, ilk atamalar yapildi.\n",
    ALG_ADLARI[algoritma]);
}
 
static void ana_menu_yaz(void)
{
    printf("\n================ TEKNIK DESTEK SIMULASYONU ================\n");
    printf("Zaman: %d (%d dk) | Algoritma: %s", saat, saat * 5, ALG_ADLARI[algoritma]);
    if (algoritma == ALG_RR) printf(" | Quantum: %d", quantum);
    printf("\n-----------------------------------------------------------\n");
    printf(" 1) Algoritma sec veya degistir\n");
    printf(" 2) Tum gorevleri listele\n");
    printf(" 3) Gorevleri durumuna gore listele\n");
    printf(" 4) Gorev ID ile sorgula\n");
    printf(" 5) Teknisyenlerin durumunu goster\n");
    printf(" 6) Zamani 1 birim ilerlet\n");
    printf(" 7) Zamani N birim ilerlet\n");
    printf(" 8) Belirli bir zamana git\n");
    printf(" 9) Istatistikleri goster\n");
    printf("10) Simulasyonu sifirla\n");
    printf(" 0) Cikis\n");
}
 
int main(void)
{
    char yol[MAX_SATIR];
    int secim;
 
    for (;;) {
        printf("Gorev dosyasinin yolunu giriniz (cikmak icin bos birakin): ");
        satir_oku(yol, sizeof yol);
        if (yol[0] == '\0') return 0;
        if (dosya_oku(yol)) break;
    }
 
    algoritma_sec();
 
    for (;;) {
        ana_menu_yaz();
        secim = tamsayi_al("Seciminiz: ", 0, 10);
 
        switch (secim) {
        case 1: algoritma_sec(); break;
        case 2: gorevleri_listele(-1); break;
        case 3: durum_listele(); break;
        case 4: id_sorgula(); break;
        case 5: teknisyenleri_goster(); break;
        case 6:
            bir_adim();
            printf("Zaman %d'e ilerletildi.\n", saat);
            break;
        case 7: {
            int n = tamsayi_al("N (pozitif): ", 1, INT_MAX);
            if (n > INT_MAX - saat) { printf("Zaman sinirini asar.\n"); break; }
            zamana_git(saat + n);
            printf("Zaman %d'e ilerletildi.\n", saat);
            break;
        }
        case 8: {
            int h = tamsayi_al("Hedef zaman (0 veya buyuk): ", 0, INT_MAX);
            if (h == saat) printf("Zaten zaman %d'desiniz, durum degismedi.\n", saat);
            else { zamana_git(h); printf("Zaman %d'e gidildi.\n", saat); }
            break;
        }
        case 9: istatistikleri_goster(); break;
        case 10:
            sim_sifirla();
            printf("Simulasyon sifirlandi (zaman 0, ilk atamalar yapildi).\n");
            break;
        case 0:
            printf("Cikis yapiliyor.\n");
            return 0;
        }
        if (secim != 6) devam_icin_bekle();
    }
}