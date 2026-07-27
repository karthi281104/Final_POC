#include "persistence.h"
#include "memory.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define DB_LINE_MAX 512U

/* ===================== Seed data (100 companies) =======================
 * Hardcoded in C (no external scripts/files needed to generate it). */
typedef struct {
    const char *symbol;
    const char *name;
    double price;
} SeedEntry;

static const SeedEntry g_seedData[SEED_STOCK_COUNT] = {
    {"AAPL","Apple Inc.",195.20},{"MSFT","Microsoft Corp.",412.65},
    {"GOOGL","Alphabet Inc. Class A",167.80},{"AMZN","Amazon.com Inc.",178.35},
    {"NVDA","NVIDIA Corp.",121.40},{"META","Meta Platforms Inc.",487.90},
    {"TSLA","Tesla Inc.",243.10},{"BRKB","Berkshire Hathaway B",412.00},
    {"AVGO","Broadcom Inc.",167.20},{"JPM","JPMorgan Chase & Co.",198.75},
    {"LLY","Eli Lilly and Co.",789.40},{"V","Visa Inc.",275.60},
    {"UNH","UnitedHealth Group",512.30},{"XOM","Exxon Mobil Corp.",114.85},
    {"MA","Mastercard Inc.",456.20},{"COST","Costco Wholesale",725.90},
    {"HD","Home Depot Inc.",345.10},{"PG","Procter & Gamble",162.40},
    {"JNJ","Johnson & Johnson",158.90},{"WMT","Walmart Inc.",68.45},
    {"NFLX","Netflix Inc.",632.10},{"MRK","Merck & Co.",118.20},
    {"ABBV","AbbVie Inc.",172.60},{"CVX","Chevron Corp.",156.30},
    {"CRM","Salesforce Inc.",302.50},{"BAC","Bank of America",38.90},
    {"PEP","PepsiCo Inc.",172.10},{"KO","Coca-Cola Co.",63.20},
    {"AMD","Advanced Micro Devices",162.80},{"TMO","Thermo Fisher",552.40},
    {"ADBE","Adobe Inc.",512.90},{"CSCO","Cisco Systems",54.30},
    {"MCD","McDonald's Corp.",289.70},{"ABT","Abbott Labs",112.40},
    {"ACN","Accenture plc",342.60},{"LIN","Linde plc",452.10},
    {"DHR","Danaher Corp.",258.90},{"WFC","Wells Fargo & Co.",62.40},
    {"TXN","Texas Instruments",198.20},{"PM","Philip Morris Intl",132.50},
    {"NEE","NextEra Energy",72.30},{"VZ","Verizon Communications",41.20},
    {"CMCSA","Comcast Corp.",42.80},{"ORCL","Oracle Corp.",178.40},
    {"INTC","Intel Corp.",34.60},{"IBM","IBM Corp.",192.30},
    {"NKE","Nike Inc.",78.90},{"UPS","United Parcel Service",132.10},
    {"RTX","RTX Corp.",112.40},{"HON","Honeywell Intl",212.60},
    {"QCOM","Qualcomm Inc.",172.30},{"AMGN","Amgen Inc.",298.70},
    {"LOW","Lowe's Companies",242.90},{"SBUX","Starbucks Corp.",92.40},
    {"CAT","Caterpillar Inc.",352.10},{"BA","Boeing Co.",178.60},
    {"GE","GE Aerospace",172.30},{"SPGI","S&P Global",458.90},
    {"ELV","Elevance Health",512.40},{"PLD","Prologis Inc.",112.60},
    {"MDT","Medtronic plc",88.30},{"BLK","BlackRock Inc.",892.10},
    {"GS","Goldman Sachs",478.60},{"AXP","American Express",242.30},
    {"BKNG","Booking Holdings",4120.50},{"SYK","Stryker Corp.",342.60},
    {"MDLZ","Mondelez Intl",68.90},{"ADI","Analog Devices",212.40},
    {"TJX","TJX Companies",112.30},{"GILD","Gilead Sciences",82.60},
    {"C","Citigroup Inc.",68.90},{"MMC","Marsh & McLennan",212.30},
    {"VRTX","Vertex Pharmaceuticals",452.10},{"REGN","Regeneron Pharma",892.40},
    {"ISRG","Intuitive Surgical",412.60},{"PGR","Progressive Corp.",232.10},
    {"CB","Chubb Ltd.",258.90},{"NOW","ServiceNow Inc.",782.30},
    {"ZTS","Zoetis Inc.",172.60},{"FI","Fiserv Inc.",158.20},
    {"BSX","Boston Scientific",78.90},{"SO","Southern Co.",88.40},
    {"DUK","Duke Energy",112.30},{"SCHW","Charles Schwab",72.60},
    {"MO","Altria Group",52.30},{"TGT","Target Corp.",142.60},
    {"CI","Cigna Group",342.10},{"DE","Deere & Co.",412.30},
    {"APD","Air Products",258.90},{"MU","Micron Technology",112.40},
    {"EOG","EOG Resources",128.60},{"SLB","Schlumberger NV",48.30},
    {"CL","Colgate-Palmolive",92.60},{"ITW","Illinois Tool Works",258.40},
    {"FDX","FedEx Corp.",278.90},{"NOC","Northrop Grumman",478.60},
    {"EMR","Emerson Electric",112.30},{"APH","Amphenol Corp.",68.90},
    {"PANW","Palo Alto Networks",312.60},{"KLAC","KLA Corp.",712.40}
};

static void trimNewline(char *line)
{
    size_t len = strlen(line);
    while ((len > 0U) && ((line[len - 1U] == '\n') || (line[len - 1U] == '\r')))
    {
        line[len - 1U] = '\0';
        len--;
    }
}

status_t saveMainDbToPath(MainCache *cache, const char *path)
{
    status_t result = STATUS_OK;

    if ((cache == NULL) || (path == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        FILE *fp = fopen(path, "w");
        if (fp == NULL)
        {
            result = STATUS_ERR_IO;
        }
        else
        {
            Stock *arr = NULL;
            size_t count = 0U;
            status_t snapResult = mainCacheSnapshot(cache, &arr, &count);
            if (snapResult == STATUS_OK)
            {
                size_t i;
                for (i = 0U; i < count; i++)
                {
                    (void)fprintf(fp, "%s|%s|%.4f|%ld\n",
                                  arr[i].symbol, arr[i].name, arr[i].price,
                                  (long)arr[i].lastUpdated);
                }
                if (arr != NULL)
                {
                    mmFree(arr);
                }
            }
            else
            {
                result = snapResult;
            }
            (void)fclose(fp);
        }
    }
    return result;
}

status_t loadMainDbFromPath(MainCache *cache, const char *path)
{
    status_t result = STATUS_OK;

    if ((cache == NULL) || (path == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        FILE *fp = fopen(path, "r");
        if (fp == NULL)
        {
            result = STATUS_ERR_IO;
        }
        else
        {
            char line[DB_LINE_MAX];
            while (fgets(line, (int)sizeof(line), fp) != NULL)
            {
                char *symbolTok;
                char *nameTok;
                char *priceTok;
                char *updatedTok;

                trimNewline(line);
                if (line[0] == '\0')
                {
                    continue;
                }

                symbolTok = strtok(line, "|");
                nameTok   = strtok(NULL, "|");
                priceTok  = strtok(NULL, "|");
                updatedTok = strtok(NULL, "|");

                if ((symbolTok != NULL) && (nameTok != NULL) && (priceTok != NULL))
                {
                    Stock st;
                    memset(&st, 0, sizeof(st));
                    (void)safe_strcpy(st.symbol, sizeof(st.symbol), symbolTok);
                    (void)safe_strcpy(st.name, sizeof(st.name), nameTok);
                    st.price = atof(priceTok);
                    st.lastUpdated = (updatedTok != NULL) ? (time_t)atol(updatedTok) : time(NULL);
                    (void)mainCacheAdd(cache, &st);
                }
            }
            (void)fclose(fp);
        }
    }
    return result;
}

status_t saveCacheToPath(SearchCache *sc, const char *path)
{
    status_t result = STATUS_OK;

    if ((sc == NULL) || (path == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        FILE *fp = fopen(path, "w");
        if (fp == NULL)
        {
            result = STATUS_ERR_IO;
        }
        else
        {
            Stock *arr = NULL;
            size_t count = 0U;
            status_t snapResult = searchCacheSnapshot(sc, &arr, &count);
            if (snapResult == STATUS_OK)
            {
                size_t i;
                /* Written MRU-first; reading back with searchCacheTouch in
                 * this same order preserves the recency ordering. */
                for (i = 0U; i < count; i++)
                {
                    (void)fprintf(fp, "%s|%s|%.4f|%ld\n",
                                  arr[i].symbol, arr[i].name, arr[i].price,
                                  (long)arr[i].lastUpdated);
                }
                if (arr != NULL)
                {
                    mmFree(arr);
                }
            }
            else
            {
                result = snapResult;
            }
            (void)fclose(fp);
        }
    }
    return result;
}

status_t loadCacheFromPath(SearchCache *sc, const char *path)
{
    status_t result = STATUS_OK;

    if ((sc == NULL) || (path == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        FILE *fp = fopen(path, "r");
        if (fp == NULL)
        {
            result = STATUS_ERR_IO;
        }
        else
        {
            char lines[SEARCH_CACHE_CAPACITY][DB_LINE_MAX];
            size_t lineCount = 0U;

            while ((lineCount < SEARCH_CACHE_CAPACITY) &&
                   (fgets(lines[lineCount], (int)sizeof(lines[lineCount]), fp) != NULL))
            {
                trimNewline(lines[lineCount]);
                if (lines[lineCount][0] != '\0')
                {
                    lineCount++;
                }
            }
            (void)fclose(fp);

            /* File is written MRU-first; insert in reverse (LRU-first) so
             * that after all touches, MRU ends up at the head again. */
            if (lineCount > 0U)
            {
                size_t i = lineCount;
                while (i > 0U)
                {
                    char *symbolTok;
                    char *nameTok;
                    char *priceTok;
                    char *updatedTok;

                    i--;
                    symbolTok = strtok(lines[i], "|");
                    nameTok   = strtok(NULL, "|");
                    priceTok  = strtok(NULL, "|");
                    updatedTok = strtok(NULL, "|");

                    if ((symbolTok != NULL) && (nameTok != NULL) && (priceTok != NULL))
                    {
                        Stock st;
                        memset(&st, 0, sizeof(st));
                        (void)safe_strcpy(st.symbol, sizeof(st.symbol), symbolTok);
                        (void)safe_strcpy(st.name, sizeof(st.name), nameTok);
                        st.price = atof(priceTok);
                        st.lastUpdated = (updatedTok != NULL) ? (time_t)atol(updatedTok) : time(NULL);
                        (void)searchCacheTouch(sc, &st);
                    }
                }
            }
        }
    }
    return result;
}

status_t saveMainDb(MainCache *cache)
{
    return saveMainDbToPath(cache, DEFAULT_STOCK_DB_PATH);
}

status_t loadMainDb(MainCache *cache)
{
    return loadMainDbFromPath(cache, DEFAULT_STOCK_DB_PATH);
}

status_t saveCacheDb(SearchCache *sc)
{
    return saveCacheToPath(sc, DEFAULT_CACHE_DB_PATH);
}

status_t loadCacheDb(SearchCache *sc)
{
    return loadCacheFromPath(sc, DEFAULT_CACHE_DB_PATH);
}

status_t persistenceSeedMainDb(MainCache *cache)
{
    status_t result = STATUS_OK;

    if (cache == NULL)
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        size_t i;
        for (i = 0U; i < SEED_STOCK_COUNT; i++)
        {
            Stock st;
            memset(&st, 0, sizeof(st));
            (void)safe_strcpy(st.symbol, sizeof(st.symbol), g_seedData[i].symbol);
            (void)safe_strcpy(st.name, sizeof(st.name), g_seedData[i].name);
            st.price = g_seedData[i].price;
            st.lastUpdated = time(NULL);
            (void)mainCacheAdd(cache, &st);
        }
        (void)loggerLog(LOG_AUDIT, "Seeded main database with %u companies",
                         (unsigned)SEED_STOCK_COUNT);
    }
    return result;
}
