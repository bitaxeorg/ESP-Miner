#include <stdint.h>
#include <stddef.h>

/*
 * BIP320 Version Rolling
 *
 * Verhoogt uitsluitend de bits die door version_mask zijn toegestaan.
 *
 * Voorbeeld:
 *   base = 0x20000000
 *   mask = 0x0000a000
 *
 * De bits in de mask worden behandeld als een kleine binaire teller.
 *
 * Dus NIET:
 *     version + mask
 *
 * maar:
 *     masked bits + 1
 *
 * Alle bits buiten de mask blijven exact hetzelfde.
 */
uint32_t increment_bitmask(uint32_t version, uint32_t mask)
{
    if (mask == 0) {
        return version;
    }

    uint32_t result = version;
    uint32_t carry = 1;

    /*
     * Loop van bit 0 t/m bit 31.
     * Alleen bits die in mask zitten worden als teller gebruikt.
     */
    for (int bit = 0; bit < 32; bit++) {

        uint32_t bit_mask = (1U << bit);

        if ((mask & bit_mask) == 0) {
            continue;
        }

        if (carry) {

            if (result & bit_mask) {
                /*
                 * 1 + 1 = 0, carry gaat naar
                 * het volgende toegestane bit.
                 */
                result &= ~bit_mask;
            } else {
                /*
                 * 0 + 1 = 1, klaar.
                 */
                result |= bit_mask;
                carry = 0;
            }
        }
    }

    /*
     * Overflow:
     * alle rollable bits waren 1.
     *
     * In dat geval wordt de masked counter
     * terug 0.
     */
    return result;
}
