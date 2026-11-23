#include "lunar.h"
#include <math.h>
#include <stdio.h>

// ---- Ho Ngoc Duc's algorithm (public domain description), timezone +7 ----
static const double TZ = 7.0;
static const double PI = 3.14159265358979323846;

static long INT(double d) { return (long)floor(d); }

long jd_from_solar(int dd, int mm, int yy)
{
    long a = (14 - mm) / 12;
    long y = yy + 4800 - a;
    long m = mm + 12 * a - 3;
    long jd = dd + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
    if (jd < 2299161) jd = dd + (153 * m + 2) / 5 + 365 * y + y / 4 - 32083;
    return jd;
}

void solar_from_jd(long jd, int &dd, int &mm, int &yy)
{
    long a, b, c;
    if (jd > 2299160) {
        a = jd + 32044;
        b = (4 * a + 3) / 146097;
        c = a - (b * 146097) / 4;
    } else {
        b = 0;
        c = jd + 32082;
    }
    long d = (4 * c + 3) / 1461;
    long e = c - (1461 * d) / 4;
    long m = (5 * e + 2) / 153;
    dd = (int)(e - (153 * m + 2) / 5 + 1);
    mm = (int)(m + 3 - 12 * (m / 10));
    yy = (int)(b * 100 + d - 4800 + m / 10);
}

int weekday_from_jd(long jd) { return (int)((jd + 1) % 7); } // JD 0 was a Monday -> +1

static double new_moon(long k)
{
    double T = k / 1236.85, T2 = T * T, T3 = T2 * T, dr = PI / 180;
    double Jd1 = 2415020.75933 + 29.53058868 * k + 0.0001178 * T2 - 0.000000155 * T3;
    Jd1 += 0.00033 * sin((166.56 + 132.87 * T - 0.009173 * T2) * dr);
    double M = 359.2242 + 29.10535608 * k - 0.0000333 * T2 - 0.00000347 * T3;
    double Mpr = 306.0253 + 385.81691806 * k + 0.0107306 * T2 + 0.00001236 * T3;
    double F = 21.2964 + 390.67050646 * k - 0.0016528 * T2 - 0.00000239 * T3;
    double C1 = (0.1734 - 0.000393 * T) * sin(M * dr) + 0.0021 * sin(2 * dr * M);
    C1 = C1 - 0.4068 * sin(Mpr * dr) + 0.0161 * sin(dr * 2 * Mpr);
    C1 = C1 - 0.0004 * sin(dr * 3 * Mpr);
    C1 = C1 + 0.0104 * sin(dr * 2 * F) - 0.0051 * sin(dr * (M + Mpr));
    C1 = C1 - 0.0074 * sin(dr * (M - Mpr)) + 0.0004 * sin(dr * (2 * F + M));
    C1 = C1 - 0.0004 * sin(dr * (2 * F - M)) - 0.0006 * sin(dr * (2 * F + Mpr));
    C1 = C1 + 0.0010 * sin(dr * (2 * F - Mpr)) + 0.0005 * sin(dr * (2 * Mpr + M));
    double deltat;
    if (T < -11)
        deltat = 0.001 + 0.000839 * T + 0.0002261 * T2 - 0.00000845 * T3 - 0.000000081 * T * T3;
    else
        deltat = -0.000278 + 0.000265 * T + 0.000262 * T2;
    return Jd1 + C1 - deltat;
}

static double sun_longitude(double jdn)
{
    double T = (jdn - 2451545.0) / 36525, T2 = T * T, dr = PI / 180;
    double M = 357.52910 + 35999.05030 * T - 0.0001559 * T2 - 0.00000048 * T * T2;
    double L0 = 280.46645 + 36000.76983 * T + 0.0003032 * T2;
    double DL = (1.914600 - 0.004817 * T - 0.000014 * T2) * sin(dr * M);
    DL = DL + (0.019993 - 0.000101 * T) * sin(dr * 2 * M) + 0.000290 * sin(dr * 3 * M);
    double L = (L0 + DL) * dr;
    L = L - PI * 2 * INT(L / (PI * 2));
    return L;
}

static long new_moon_day(long k) { return INT(new_moon(k) + 0.5 + TZ / 24); }
static int sun_long_sector(long day) { return (int)INT(sun_longitude(day - 0.5 - TZ / 24) / PI * 6); }

static long lunar_month11(int yy)
{
    long off = jd_from_solar(31, 12, yy) - 2415021;
    long k = INT(off / 29.530588853);
    long nm = new_moon_day(k);
    if (sun_long_sector(nm) >= 9) nm = new_moon_day(k - 1);
    return nm;
}

static int leap_month_offset(long a11)
{
    long k = INT((a11 - 2415021.076998695) / 29.530588853 + 0.5);
    int last, i = 1;
    int arc = sun_long_sector(new_moon_day(k + i));
    do {
        last = arc;
        i++;
        arc = sun_long_sector(new_moon_day(k + i));
    } while (arc != last && i < 14);
    return i - 1;
}

lunar_date_t lunar_from_solar(int dd, int mm, int yy)
{
    lunar_date_t r;
    long day = jd_from_solar(dd, mm, yy);
    r.jd = day;
    long k = INT((day - 2415021.076998695) / 29.530588853);
    long month_start = new_moon_day(k + 1);
    if (month_start > day) month_start = new_moon_day(k);
    long a11 = lunar_month11(yy), b11 = a11;
    if (a11 >= month_start) {
        r.year = yy;
        a11 = lunar_month11(yy - 1);
    } else {
        r.year = yy + 1;
        b11 = lunar_month11(yy + 1);
    }
    r.day = (int)(day - month_start + 1);
    int diff = (int)INT((month_start - a11) / 29.0);
    r.leap = false;
    r.month = diff + 11;
    if (b11 - a11 > 365) {
        int leap_diff = leap_month_offset(a11);
        if (diff >= leap_diff) {
            r.month = diff + 10;
            if (diff == leap_diff) r.leap = true;
        }
    }
    if (r.month > 12) r.month -= 12;
    if (r.month >= 11 && diff < 4) r.year -= 1;
    return r;
}

// ---- Can Chi ----
static const char *CAN[10] = {"Giáp", "Ất", "Bính", "Đinh", "Mậu", "Kỷ", "Canh", "Tân", "Nhâm", "Quý"};
static const char *CHI[12] = {"Tý", "Sửu", "Dần", "Mão", "Thìn", "Tỵ", "Ngọ", "Mùi", "Thân", "Dậu", "Tuất", "Hợi"};

void lunar_year_canchi(int y, char *out, size_t n)
{
    snprintf(out, n, "%s %s", CAN[(y + 6) % 10], CHI[(y + 8) % 12]);
}

void lunar_month_canchi(int m, int y, char *out, size_t n)
{
    snprintf(out, n, "%s %s", CAN[(y * 12 + m + 3) % 10], CHI[(m + 1) % 12]);
}

void lunar_day_canchi(long jd, char *out, size_t n)
{
    snprintf(out, n, "%s %s", CAN[(jd + 9) % 10], CHI[(jd + 1) % 12]);
}

// ---- Holidays ----
struct holiday_t { int d, m; const char *name; };

static const holiday_t LUNAR_HOLIDAYS[] = {
    {1, 1, "Tết Nguyên Đán"},  {15, 1, "Rằm tháng Giêng"}, {10, 3, "Giỗ Tổ Hùng Vương"},
    {15, 4, "Lễ Phật Đản"},    {5, 5, "Tết Đoan Ngọ"},     {15, 7, "Lễ Vu Lan"},
    {15, 8, "Tết Trung Thu"},  {23, 12, "Ông Công Ông Táo"},
};

static const holiday_t SOLAR_HOLIDAYS[] = {
    {1, 1, "Tết Dương lịch"},     {14, 2, "Lễ Tình nhân"},      {8, 3, "Quốc tế Phụ nữ"},
    {30, 4, "Giải phóng miền Nam"}, {1, 5, "Quốc tế Lao động"}, {1, 6, "Quốc tế Thiếu nhi"},
    {2, 9, "Quốc khánh"},          {20, 10, "Phụ nữ Việt Nam"},  {20, 11, "Nhà giáo Việt Nam"},
    {24, 12, "Giáng sinh"},
};

// Keeps out.holidays sorted by date (first LUNAR_NEXT_HOLIDAYS only).
static void consider(lunar_event_t &best, long jd, const char *name, long today, lunar_upcoming_t &out,
                     bool lunar)
{
    if (jd < today) return;
    if (jd == today && !out.today_holiday) out.today_holiday = name;
    if (best.jd == 0 || jd < best.jd) {
        best.jd = jd;
        best.name = name;
    }
    int n = out.n_holidays, i = n;
    while (i > 0 && out.holidays[i - 1].jd > jd) i--;
    if (i >= LUNAR_NEXT_HOLIDAYS) return;
    if (n == LUNAR_NEXT_HOLIDAYS) n--;           // drop the last one
    for (int j = n; j > i; j--) {
        out.holidays[j] = out.holidays[j - 1];
        out.holiday_lunar[j] = out.holiday_lunar[j - 1];
    }
    out.holidays[i] = {jd, name};
    out.holiday_lunar[i] = lunar;
    out.n_holidays = n + 1;
}

void lunar_upcoming(long today, lunar_upcoming_t &out)
{
    out.ram = {0, "Rằm"};
    out.mung1 = {0, "Mùng 1"};
    out.holiday = {0, ""};
    out.today_holiday = nullptr;
    out.n_holidays = 0;

    // Walk the next 14 lunar months, starting with the current one.
    long k = INT((today - 2415021.076998695) / 29.530588853);
    if (new_moon_day(k + 1) <= today) k++;
    else if (new_moon_day(k) > today) k--;

    for (int i = 0; i < 14; i++) {
        long start = new_moon_day(k + i);
        int d, m, y;
        solar_from_jd(start, d, m, y);
        lunar_date_t L = lunar_from_solar(d, m, y);

        if (!out.mung1.jd && start >= today) out.mung1.jd = start;
        if (!out.ram.jd && start + 14 >= today) out.ram.jd = start + 14;

        if (L.leap) continue; // holidays fall in the regular month
        for (const holiday_t &h : LUNAR_HOLIDAYS)
            if (h.m == L.month) consider(out.holiday, start + h.d - 1, h.name, today, out, true);
        if (L.month == 1) consider(out.holiday, start - 1, "Giao thừa", today, out, true);
    }

    int td, tm, ty;
    solar_from_jd(today, td, tm, ty);
    for (const holiday_t &h : SOLAR_HOLIDAYS) {
        long jd = jd_from_solar(h.d, h.m, ty);
        if (jd < today) jd = jd_from_solar(h.d, h.m, ty + 1);
        consider(out.holiday, jd, h.name, today, out, false);
    }
}

// ---- Solar terms: 24 sectors of 15 deg of the sun's longitude, from 0 deg ----
static const char *TIET_KHI[24] = {
    "Xuân phân", "Thanh minh", "Cốc vũ", "Lập hạ", "Tiểu mãn", "Mang chủng",
    "Hạ chí", "Tiểu thử", "Đại thử", "Lập thu", "Xử thử", "Bạch lộ",
    "Thu phân", "Hàn lộ", "Sương giáng", "Lập đông", "Tiểu tuyết", "Đại tuyết",
    "Đông chí", "Tiểu hàn", "Đại hàn", "Lập xuân", "Vũ thủy", "Kinh trập",
};

const char *lunar_solar_term(long jd)
{
    // at the end of the day (local midnight after it): a term starting during
    // the day already shows on that day, like printed calendars
    int i = (int)INT(sun_longitude(jd + 1 - 0.5 - TZ / 24) / PI * 12);
    return TIET_KHI[((i % 24) + 24) % 24];
}

long lunar_tet_jd(long today, int &ly)
{
    int d, m, y;
    solar_from_jd(today, d, m, y);
    lunar_date_t L = lunar_from_solar(d, m, y);
    if (L.month == 1 && !L.leap && L.day <= 3) {
        ly = L.year;
        return today - (L.day - 1);
    }
    long k = INT((today - 2415021.076998695) / 29.530588853);
    for (int i = 0; i < 16; i++) {
        long start = new_moon_day(k + i);
        if (start <= today) continue;
        solar_from_jd(start, d, m, y);
        lunar_date_t S = lunar_from_solar(d, m, y);
        if (S.month == 1 && !S.leap && S.day == 1) {
            ly = S.year;
            return start;
        }
    }
    return 0;
}

long lunar_next_date(long today, int lday, int lmonth, int &ly)
{
    if (lday < 1 || lday > 30 || lmonth < 1 || lmonth > 12) return 0;
    long k = INT((today - 2415021.076998695) / 29.530588853);
    if (new_moon_day(k + 1) <= today) k++;
    else if (new_moon_day(k) > today) k--;
    for (int i = 0; i < 15; i++) {
        long start = new_moon_day(k + i);
        int d, m, y;
        solar_from_jd(start, d, m, y);
        lunar_date_t L = lunar_from_solar(d, m, y);
        if (L.leap || L.month != lmonth) continue;
        long last = new_moon_day(k + i + 1) - 1;  // last day of this lunar month
        long jd = start + lday - 1;
        if (jd > last) jd = last;
        if (jd >= today) {
            ly = L.year;
            return jd;
        }
    }
    return 0;
}
