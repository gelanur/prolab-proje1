#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>


#define MAX_GOREV      5000   /* En az 1000 gorev istendi; kapasite 5000     */
#define MAX_SATIR      512    /* Dosyadan okunabilecek en uzun satir          */
#define MAX_TUR        40     /* Gorev turu: en fazla 39 karakter + '\0'      */
#define TEKNISYEN_SAY  4      /* T1, T2, T3, T4                               */
#define KONUM_SAY      5
#define SAYFA_BOYU     20     /* Listelerde bir sayfada gosterilen gorev      */

#ifndef GECIS_SURESI
#define GECIS_SURESI   1      /* Konum degisim maliyeti (birim)               */
#endif


typedef enum { HENUZ_GELMEDI = 0, BEKLIYOR, DEVAM_EDIYOR, TAMAMLANDI } Durum;

/* Secili algoritma */
typedef enum { ALG_YOK = 0, ALG_SJ, ALG_PRIORITY, ALG_RR } Algoritma;

/* Teknisyenin durumu */
typedef enum { T_BOS = 0, T_GECIS, T_CALISIYOR } TeknisyenDurum;

typedef struct {
 /* Dosyadan okunan sabit bilgiler */
int  id;
int  gelis;            /* GELIS_ZAMANI */
int  sure;             /* SURE (toplam calisma) */
int  oncelik;          /* 1 (en onemli) .. 5 */
char tur[MAX_TUR];     /* GOREV_TURU */
int  konum;            /* KONUMLAR dizisindeki indeks */

 /* Simulasyon sirasinda degisen bilgiler */
Durum durum;
int  kalan;            /* Kalan calisma suresi */
int  teknisyen;        /* Su anki teknisyen (0..3), yoksa -1 */
int  ilk_baslama;      /* Calismanin ilk basladigi an, yoksa -1 */
int  bitis;            /* Tamamlanma zamani, yoksa -1 */
int  bitiren;          /* Gorevi bitiren teknisyen, yoksa -1 */
} Gorev;

typedef struct {
TeknisyenDurum durum;
int gorev;             /* gorevler[] dizisindeki indeks, yoksa -1 */
int konum;             /* Son bulundugu konum, baslangicta -1 (hicbir yer) */
int gecis_kalan;       /* Gecis durumunda kalan sure */
int dilim;             /* RR: mevcut dilimde calisilan birim */
int calisilan;         /* Simdiye kadar calisilan toplam birim */
int gecis_sayisi;      /* Kac kez konum degistirdi */
} Teknisyen;

static const char *KONUMLAR[KONUM_SAY] = {
"LAB_A", "LAB_B", "DERSLIK_1", "DERSLIK_2", "DERSLIK_3"
};
static const char *DURUM_ADLARI[4] = {
"Henuz gelmedi", "Bekliyor", "Devam ediyor", "Tamamlandi"
};
static const char *ALG_ADLARI[4] = {
"Secilmedi", "Shortest Job", "Priority", "Round Robin"
};

static Gorev     gorevler[MAX_GOREV];
static int       gorev_sayisi = 0;
static Teknisyen tek[TEKNISYEN_SAY];
static int       saat = 0;
static Algoritma algoritma = ALG_YOK;
static int       quantum = 0;

/* Round Robin icin dairesel kuyruk. Bir gorev kuyrukta en fazla bir kez
 * bulunabildigi icin MAX_GOREV boyutu yeterlidir. */
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

/* Klavyeden bir satir okur. Satir sonundaki '\n' silinir; tampona
 * sigmayan fazlalik atilir. Girdi biterse (EOF) program kapanir. */
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

/* Metni tam sayiya cevirir. Basarili ise 1, aksi halde 0 dondurur.
 * "12abc", "", "3.5" ve int sinirini asan degerler gecersizdir. */
static int tamsayi_cevir(const char *s, int *sonuc)
{
    char *son;
    long v;
    if (s == NULL) return 0;
    errno = 0;
    v = strtol(s, &son, 10);
    if (son == s) return 0;                         /* hic rakam yok */
    while (isspace((unsigned char)*son)) son++;
    if (*son != '\0') return 0;                     /* fazladan karakter */
    if (errno == ERANGE || v < INT_MIN || v > INT_MAX) return 0;
    *sonuc = (int)v;
    return 1;
}

/* Kullanicidan [alt, ust] araliginda bir tam sayi alir; gecersizse tekrar sorar. */
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

/* qsort icin: gorevleri ID'ye gore sirala */
static int id_karsilastir(const void *a, const void *b)
{
    const Gorev *x = (const Gorev *)a, *y = (const Gorev *)b;
    return (x->id > y->id) - (x->id < y->id);
}

/* Sirali dizide ID ile ikili arama. Bulunamazsa -1. */
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

//dosya okuma fonksiyonu
static int dosya_oku(const char *yol)
{
    FILE *fp;
    char satir[MAX_SATIR];
    int satir_no = 0, reddedilen = 0, i;

    fp = fopen(yol, "r");   /* Dosya yalnizca okunur, degistirilmez */
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

        /* Tampona sigmayan cok uzun satir kontrolu */
        if (n > 0 && satir[n - 1] != '\n') {
            int c = fgetc(fp);
            if (c != EOF) {                 /* dosya bitmedi -> satir uzun */
                while (c != '\n' && c != EOF) c = fgetc(fp);
                printf("Satir %d reddedildi: satir cok uzun.\n", satir_no);
                reddedilen++;
                continue;
            }
        }

        /* Bos satir ve ilk dolu karakteri '#' olan aciklama satirlari atlanir */
        p = satir;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '#') continue;

        /* Alanlari bosluk veya sekmeye gore ayir */
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

        /* Her alanin dogrulanmasi */
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

        /* Yinelenen ID: ilk gecerli kayit korunur */
        for (i = 0; i < gorev_sayisi; i++)
            if (gorevler[i].id == id) break;
        if (i < gorev_sayisi) {
            printf("Satir %d reddedildi: yinelenen ID %d (ilk kayit korundu).\n",satir_no, id);
            reddedilen++; continue;
        } 
        /* Kapasite kontrolu */
        if (gorev_sayisi >= MAX_GOREV) {
            printf("Satir %d reddedildi: kapasite asildi (en fazla %d gorev).\n", satir_no, MAX_GOREV);
            reddedilen++; continue;
        }

        /* Gecerli kayit -> diziye ekle */
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

    /* Dosya geliş zamanina gore sirali olmak zorunda degil. Gorevleri ID'ye
     * gore siraliyoruz: listeler ID sirasinda olur, ayni anda gelenler de
     * dogal olarak kucuk ID sirasiyla islenir. */
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
 
/* SJ/Priority: bekleyen gorevler arasindan en uygununu bulur, yoksa -1 */
static int bekleyen_sec(void)
{
    int i, en = -1;
    for (i = 0; i < gorev_sayisi; i++) {
        if (gorevler[i].durum != BEKLIYOR) continue;
        if (en == -1 || daha_oncelikli(&gorevler[i], &gorevler[en])) en = i;
    }
    return en;
}
 
/* g gorevini t teknisyenine atar. Konum farkliysa teknisyen once gecise girer. */
static void gorev_ata(int t, int g)
{
    Teknisyen *tk = &tek[t];
    Gorev *gv = &gorevler[g];
 
    gv->durum = DEVAM_EDIYOR;
    gv->teknisyen = t;
    tk->gorev = g;
    tk->dilim = 0;                       /* RR dilim sayaci her atamada sifirlanir */
 
    if (tk->konum != -1 && tk->konum != gv->konum && GECIS_SURESI > 0) {
        /* Farkli konum: once gecis yapilacak, gorev henuz ilerlemiyor */
        tk->durum = T_GECIS;
        tk->gecis_kalan = GECIS_SURESI;
        tk->gecis_sayisi++;
    } else {
        /* Ayni konum (veya ilk gorev): hemen calismaya baslar */
        tk->durum = T_CALISIYOR;
        if (gv->ilk_baslama == -1) gv->ilk_baslama = saat;
    }
    tk->konum = gv->konum;               /* Teknisyen artik bu konuma gidiyor */
}
 
/* Bos teknisyenlere T1, T2, T3, T4 sirasiyla gorev atar */
static void atamalari_yap(void)
{
    int t;
    for (t = 0; t < TEKNISYEN_SAY; t++) {
        int g;
        if (tek[t].durum != T_BOS) continue;
        if (algoritma == ALG_RR)
            g = (k_say > 0) ? kuyruk_cikar() : -1;   /* ortak kuyrugun basi */
        else
            g = bekleyen_sec();
        if (g == -1) break;              /* bekleyen gorev kalmadi */
        gorev_ata(t, g);
    }
}
 
/* Geliş zamani gelen gorevleri "Bekliyor" yapar.
 * Dizi ID'ye gore sirali oldugundan ayni anda gelenler kucuk ID sirasiyla
 * RR kuyrugunun sonuna eklenir. */
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
 
/* Simulasyonu zaman 0'a dondurur, tum durum ve istatistikleri temizler,
 * ardindan zaman 0'daki gelisleri ve ilk atamalari yapar. */
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
 
/* Zamani 1 birim ilerletir. Islem sirasi projedeki kurallara gore:
 *  1) [saat, saat+1) araliginda calisan gorevler ilerler, gecisler azalir
 *  2) saat artar
 *  3) biten gorevler tamamlanir; RR'de quantumu dolanlar teknisyenden ayrilir
 *  4) yeni gelisler kuyruga eklenir (kucuk ID sirasiyla)
 *  5) RR: quantumu dolanlar ayrildiklari teknisyen sirasiyla kuyruga eklenir
 *  6) bos teknisyenlere atama yapilir */
static void bir_adim(void)
{
    int dolan[TEKNISYEN_SAY], dolan_say = 0, t;
 
    /* 1) Bir birimlik calisma */
    for (t = 0; t < TEKNISYEN_SAY; t++) {
        Teknisyen *tk = &tek[t];
        if (tk->durum == T_GECIS) {
            tk->gecis_kalan--;
            if (tk->gecis_kalan == 0) {
                /* Gecis bitti: gorev bir sonraki andan itibaren ilerler */
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
 
    /* 2) Saat ilerler */
    saat++;
 
    /* 3) Bitenler ve quantumu dolanlar */
    for (t = 0; t < TEKNISYEN_SAY; t++) {
        Teknisyen *tk = &tek[t];
        Gorev *g;
        if (tk->durum != T_CALISIYOR) continue;
        g = &gorevler[tk->gorev];
 
        if (g->kalan == 0) {
            /* Gorev bitti (quantum ile ayni anda bitse de tamamlanir) */
            g->durum = TAMAMLANDI;
            g->bitis = saat;
            g->bitiren = t;
            g->teknisyen = -1;
            tk->durum = T_BOS;
            tk->gorev = -1;
            tk->dilim = 0;
        } else if (algoritma == ALG_RR && tk->dilim >= quantum) {
            /* Quantum doldu, gorev bitmedi: kalan suresi korunarak bekler */
            g->durum = BEKLIYOR;
            g->teknisyen = -1;
            dolan[dolan_say++] = tk->gorev;
            tk->durum = T_BOS;
            tk->gorev = -1;
            tk->dilim = 0;
        }
    }
 
    /* 4) Yeni gelisler */
    gelenleri_isle();
 
    /* 5) RR: quantumu dolanlar teknisyen numarasi sirasiyla kuyruk sonuna */
    for (t = 0; t < dolan_say; t++) kuyruk_ekle(dolan[t]);
 
    /* 6) Atamalar */
    atamalari_yap();
}
 
/* Hedef zamana gider. Gecmise donuluyorsa basa donup yeniden hesaplar. */
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
    else strcpy(tk, "-");          /* Bekleyen/gelmemis gorevde teknisyen bos */
    printf("%-7d %-6d %-5d %-8d %-14s %-10s %-6d\n",g->id, g->gelis, g->sure, g->oncelik, DURUM_ADLARI[g->durum], tk, g->kalan);
}
 
/* filtre = -1 ise tum gorevler, aksi halde sadece o durumdakiler. Sayfalamali. */
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
            /* Bekleme = bitis - geliş - sure (RR'deki tum beklemeler dahil) */
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
        printf("Son bitis zamani     : %d  (makespan - tum gorevler tamamlandi)\n", son_bitis);
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
int main() {


return 0;
}