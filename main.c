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

int main() {


return 0;
}