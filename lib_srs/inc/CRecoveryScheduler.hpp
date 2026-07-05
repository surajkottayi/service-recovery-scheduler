#ifndef CRecoveryScheduler_HPP
#define CRecoveryScheduler_HPP

#include <memory>

namespace lib_srs
{

    class CRecoveryScheduler
    {
    public:
        [[nodiscard]] static std::shared_ptr<CRecoveryScheduler> getInstance();
        ~CRecoveryScheduler() = default;

    private:
        CRecoveryScheduler() = default;
        CRecoveryScheduler(const CRecoveryScheduler &) = delete;
        CRecoveryScheduler &operator=(const CRecoveryScheduler &) = delete;
        CRecoveryScheduler(CRecoveryScheduler &&) = delete;
        CRecoveryScheduler &operator=(CRecoveryScheduler &&) = delete;
    };

} // namespace lib_srs

#endif // CRecoveryScheduler_HPP