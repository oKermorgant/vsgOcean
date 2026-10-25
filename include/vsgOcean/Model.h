#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/Export.h>
#include <vsgOcean/Frame.h>

#include <map>
#include <string>

namespace vsgOcean
{
    /// A first-class model: a Frame (its base frame, positioned with respect to a parent model)
    /// holding a tree of named child frames with meshes attached.
    ///
    /// Frames are registered by name when the model is built (e.g. from an SDF file); their pose
    /// can then be changed at runtime via setPose(). Fixed transforms (e.g. the pose of a mesh
    /// within its frame) are not registered and cannot be changed.
    ///
    /// Models can be nested: a submodel is positioned with respect to its parent model, forming
    /// a tree of models rooted at the world model (vsgOcean::Scene).
    class VSG_OCEAN_EXPORT Model : public vsg::Inherit<Frame, Model>
    {
    public:
        explicit Model(const std::string& in_name, const vsg::dmat4& in_matrix = {});

        /// Name of the parent model's frame; empty for the world model.
        const std::string& parentName() const { return parentName_; }
        void setParentName(const std::string& parentName) { parentName_ = parentName; }

        /// Register a child frame under this model. parentFrameName is the name of the frame the
        /// child is attached to (empty, or this model's own name, means the model's base frame);
        /// it is remembered so that Model::setPose() can validate or guess the parent frame.
        void addFrame(const vsg::ref_ptr<Frame>& frame, const std::string& parentFrameName = {});

        /// Attach a submodel, positioned with respect to this model.
        void addModel(const vsg::ref_ptr<Model>& submodel);

        /// Look up a frame by name: the model itself under its own name, or any registered frame
        /// of this model or of its submodels. Returns null if unknown.
        vsg::ref_ptr<Frame> frame(const std::string& frameName) const;

        /// Look up a model by name: this model under its own name, or any of its submodels (recursively).
        vsg::ref_ptr<Model> model(const std::string& modelName) const;

        /// Set the pose of a named frame of this model or of its submodels, relative to the frame
        /// it is attached to. When parentFrameName is given it is checked against the parent frame
        /// recorded when the model was loaded (warn on mismatch); it is guessed when left empty.
        void setPose(const std::string& frameName, const vsg::dmat4& pose, const std::string& parentFrameName = "");

        /// Registered child frames by name (frames of submodels not included).
        const std::map<std::string, vsg::ref_ptr<Frame>>& frames() const { return frames_; }

        /// Direct submodels by name.
        const std::map<std::string, vsg::ref_ptr<Model>>& models() const { return models_; }

    protected:
        std::map<std::string, vsg::ref_ptr<Frame>> frames_;
        std::map<std::string, std::string> frameParents_;
        std::map<std::string, vsg::ref_ptr<Model>> models_;
        std::string parentName_;
    };
}
