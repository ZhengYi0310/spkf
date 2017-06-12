/*
 * Copyright(C) 2017. Shehryar Khurshid <shehryar87@hotmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3, as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranties of
 * MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#pragma once

#include "kf_base.h"

namespace spkf {

    /* forward declaration */
    template <class Derived>
    class SigmaBase;

    /* SigmaBase traits */
    template <class Derived>
    struct traits<SigmaBase<Derived>> {

        using scalar_t = typename traits<Derived>::scalar_t;        /* floating-point type */
        constexpr static const unsigned nx = traits<Derived>::nx;   /* state vector dims */
        constexpr static const unsigned nu = traits<Derived>::nu;   /* control vector dims */
        constexpr static const unsigned nz = traits<Derived>::nz;   /* observation vector dims */
    };

    template <class Derived>
    class SigmaBase : public KFBase<SigmaBase<Derived>> {

    public:
        EIGEN_MAKE_ALIGNED_OPERATOR_NEW

        friend class KFBase<SigmaBase<Derived>>;

        using Base = KFBase<SigmaBase<Derived>>;
        constexpr static const unsigned nx = Base::nx;
        constexpr static const unsigned nu = Base::nu;
        constexpr static const unsigned nz = Base::nz;
        constexpr static const unsigned L = nx+nx+nz;               /* augmented state vector dimension */
        constexpr static const unsigned r = 2*L + 1;                /* no. of sigma points */

        using scalar_t = typename Base::scalar_t;                   /* floating-point scalar type */
        using state_t = typename Base::state_t;                     /* state vector type */
        using control_t = typename Base::control_t;                 /* control vector type */
        using obs_t = typename Base::obs_t;                         /* observation vector type */
        using covar_t = typename Base::covar_t;                     /* state covariance matrix type */
        using obs_covar_t = typename Base::obs_covar_t;             /* observation covariance matrix type */
        using cross_covar_t = typename Base::cross_covar_t;         /* cross covariance matrix type */
        using state_sigmas_t = Matrix<scalar_t, nx, r>;             /* state sigma points matrix type */
        using obs_sigmas_t = Matrix<scalar_t, nz, r>;               /* observation sigma points matrix type */
        using aug_vector_t = Matrix<scalar_t, L, 1>;                /* augmented vector */
        using aug_matrix_t = Matrix<scalar_t, L, L>;                /* augmented matrix */
        using aug_sigmas_t = Matrix<scalar_t, L, r>;                /* augmented sigmas matrix type */

        /* default constructor */
        SigmaBase() = default;

        /* constructor */
        SigmaBase(const Ref<const state_t> &state,
                  const Ref<const covar_t> &covar,
                  const Ref<const covar_t> &proc_covar,
                  const Ref<const obs_covar_t> & obs_covar) : Base(state, covar, proc_covar, obs_covar) {

            /* Cholesky decomposition */
            LLT<covar_t> covar_llt(covar.template triangularView<Lower>());
            LLT<covar_t> proc_covar_llt(proc_covar.template triangularView<Lower>());
            LLT<obs_covar_t> obs_covar_llt(obs_covar.template triangularView<Lower>());

            /* lower triangular Cholesky covariance factors */
            _chol_covar = covar_llt.matrixL();
            _chol_proc_covar = proc_covar_llt.matrixL();
            _chol_obs_covar = obs_covar_llt.matrixL();
        }

    protected:
        /* accessors */
        inline const Ref<state_sigmas_t> state_sigmas() const { return _state_sigmas; }
        inline const Ref<state_sigmas_t> proc_noise_sigmas() const { return _proc_noise_sigmas; }
        inline const Ref<obs_sigmas_t> obs_noise_sigmas() const { return _obs_noise_sigmas; }
        inline const obs_sigmas_t& obs_sigmas() const { return _obs_sigmas; }
        inline const Ref<covar_t> chol_covar() const { return _chol_covar; }
        inline const Ref<covar_t> chol_proc_covar() const { return _chol_proc_covar; }
        inline const Ref<obs_covar_t> chol_obs_covar() const { return _chol_obs_covar; }
        inline const cross_covar_t& cross_covar() const { return _cross_covar; }

    private:

        inline bool _process(Ref<state_t> state_k,
                             const Ref<const control_t> &control_k,
                             const scalar_t del_k) {

            /* generate sigma points */
            const auto& covar_k = this->covar();
            _generate_sigmas(state_k, covar_k);

            /* propagate first sigma point through process model */
            const auto& noise_0 = _proc_noise_sigmas.col(0);
            const_derived()._f(_state_sigmas.col(0), control_k, noise_0, del_k);
            state_k = const_derived().wm0() * _state_sigmas.col(0);

            /* propagate the remaining 2*L sigma points through process model */
            for (unsigned i = 1; i < r; ++i) {

                const auto& noise_i = _proc_noise_sigmas.col(i);
                const_derived()._f(_state_sigmas.col(i), control_k, noise_i, del_k);

                /* accumulate state mean */
                state_k += const_derived().wmi() * _state_sigmas.col(i);
            }

            return true;
        }

        inline bool _process_covar(Ref<covar_t> covar_k) {

            /* compute process covariance */
            derived()._process_covar_sp(covar_k, _chol_covar);
            return true;
        }

        inline bool _observe(Ref<obs_t> observation_k) {

            /* regenerate sigma points */
            const auto& state_k = this->state();
            const auto& covar_k = this->covar();
            _generate_sigmas(state_k, covar_k);

            /* propagate first sigma point through observation model */
            const auto& noise_0 = _obs_noise_sigmas.col(0);
            const_derived()._h(_state_sigmas.col(0), _obs_sigmas.col(0), noise_0);
            observation_k = const_derived().wm0() * _obs_sigmas.col(0);

            /* propagate the remaining 2*L sigma point through observation model */
            for (unsigned i = 1; i < r; ++i) {

                const auto& noise_i = _obs_noise_sigmas.col(i);
                const_derived()._h(_state_sigmas.col(i), _obs_sigmas.col(i), noise_i);

                /* accumulate observation mean */
                observation_k += const_derived().wmi() * _obs_sigmas.col(i);
            }

            return true;
        }

        inline bool _innovation_covar(Ref<obs_covar_t> inov_covar_k) {

            /* compute innovation covariance */
            derived()._innovation_covar_sp(inov_covar_k);
            return true;
        }

        inline bool _kalman_gain(Ref<cross_covar_t> kalman_gain_k) {

            /* compute kalman gain */
            derived()._kalman_gain_sp(kalman_gain_k, _cross_covar);
            return true;
        }

        inline bool _update_covar(Ref<covar_t> covar_k) {

            /* state covariance update */
            derived()._update_covar_sp(covar_k, _chol_covar);
            return true;
        }

        inline bool _generate_sigmas(const Ref<const state_t> &state_k,
                                     const Ref<const covar_t> &covar_k) {

            /* lower Cholesky factor of state covariance */
            LLT<covar_t> covar_llt(covar_k.template triangularView<Lower>());
            _chol_covar = covar_llt.matrixL();

            const auto& proc_noise_k = this->proc_noise();
            const auto& obs_noise_k = this->obs_noise();

            /* populate augmented vector */
            _aug_vector.template block<nx,1>(0,0) = state_k;
            _aug_vector.template block<nx,1>(nx,0) = proc_noise_k;
            _aug_vector.template block<nz,1>(2*nx,0) = obs_noise_k;


            /* set first to augmented vector */
            _aug_sigmas.col(0) = _aug_vector;

            const auto sqrt_gamma = sqrt(const_derived().gamma());
            for (unsigned i = 1; i <= L; ++i) {

                _aug_sigmas.col(i) = _aug_vector + sqrt_gamma * _aug_matrix.col(i-1);
                _aug_sigmas.col(i+L) = _aug_vector - sqrt_gamma * _aug_matrix.col(i-1);
            }

            return true;
        }

        aug_vector_t        _aug_vector;
        aug_matrix_t        _aug_matrix;
        aug_sigmas_t        _aug_sigmas;

        Ref<state_sigmas_t> _state_sigmas = _aug_sigmas.block(0,0,nx,r);
        Ref<state_sigmas_t> _proc_noise_sigmas = _aug_sigmas.block(nx,0,nx,r);
        Ref<obs_sigmas_t>   _obs_noise_sigmas = _aug_sigmas.block(2*nx,0,nz,r);
        obs_sigmas_t        _obs_sigmas;

        Ref<covar_t>        _chol_covar = _aug_matrix.block(0,0,nx,nx);
        Ref<covar_t>        _chol_proc_covar = _aug_matrix.block(nx,nx,nx,nx);
        Ref<obs_covar_t>    _chol_obs_covar = _aug_matrix.block(2*nx,2*nx,nz,nz);

        cross_covar_t       _cross_covar;

        /* reference to derived object */
        inline Derived& derived() {
            return *static_cast<Derived*>(this);
        }

        /* const reference to derived object */
        inline const Derived& const_derived() const {
            return *static_cast<const Derived*>(this);
        }
    };
}