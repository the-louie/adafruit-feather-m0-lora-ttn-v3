Here are four test vectors designed to exercise every branch of your Protocol V5 logic, including the reboot detection, voltage limits, and temperature "magic" values.

### Test Vector Table

| Case | FPort | Hex Payload | Expected JSON Logic |
| --- | --- | --- | --- |
| **1. Fresh Startup** | `10` | `00 00 32 FA FA FA FA FA` | **PROD Mode**. 3.0V Battery. Seq 0 (**Reboot: true**). Temp: 0.0°C followed by 5 nulls. |
| **2. Cold Peak** | `20` | `4B 05 00 05 0A 0F 14 19` | **DEV Mode**. 4.2V Battery. Seq 5. Temps: -10.0, -9.0, -8.0, -7.0, -6.0, -5.0°C. |
| **3. Summer Max** | `10` | `BB 8A C8 C3 BE B9 B4 AF` | **PROD Mode**. 6.0V Battery. Seq 10. Temps: 30.0, 29.0, 28.0, 27.0, 26.0, 25.0°C. |
| **4. Error States** | `10` | `FF FF FA FB FC 32 FA FA` | **PROD Mode**. 7.095V (Max). Seq 15. Temps: Null, < -10, > +30, 0.0°C, Null, Null. |

---

### Verification Details

**Test Vector #1 (The "Fast-Flush"):**

* **Payload:** `00 00 32 FA FA FA FA FA`
* **Battery:** Offset 0 = **3.000V**
* **Sequence:** 0 (**Reboot detected**)
* **Temps:** `0x32` (50) is exactly **0.0°C**. Remaining are `250` (Null).

**Test Vector #2 (The "LiPo Peak"):**

* **Payload:** `4B 05 00 05 0A 0F 14 19`
* **Battery:** Offset `0x4B0` (1200) + 3000 = **4.200V**
* **Sequence:** 5
* **Temps:** `0, 5, 10, 15, 20, 25` mapped via `(v * 0.2) - 10`.

**Test Vector #4 (The "Boundary Check"):**

* **Payload:** `FF FF FA FB FC 32 FA FA`
* **Battery:** Offset `0xFFF` (4095) + 3000 = **7.095V**
* **Sequence:** 15 (Max)
* **Temps:** Demonstrates the logic jumping between null (`FA`), too cold (`FB`), and too warm (`FC`).

---

### Final Implementation Checklist for Bodforss Consulting

* **TTN Console:** Ensure you have selected the "Cayenne LPP" option **OFF** and "Javascript" **ON**.
* **FPorts:** Ensure your gateway isn't filtering FPorts 10 and 20.
* **Clamping:** Confirm that if the battery voltage reads slightly below 3.0V, the firmware sends `0x000` rather than a negative integer (the code I reviewed handles this correctly with the `if (offset < 0) offset = 0;` check).

**Would you like me to help you draft the master-plan update for the `.cursor/skills/master-plan/SKILL.md` file to reflect these final V5 specs?**