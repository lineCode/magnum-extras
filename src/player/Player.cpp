/*
    This file is part of Magnum.

    Copyright © 2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018
              Vladimír Vondruš <mosra@centrum.cz>

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
*/

#include <Corrade/Containers/Array.h>
#include <Corrade/Interconnect/Receiver.h>
#include <Corrade/PluginManager/Manager.h>
#include <Corrade/Utility/Arguments.h>
#include <Corrade/Utility/Format.h>
#include <Magnum/Mesh.h>
#include <Magnum/PixelFormat.h>
#include <Magnum/Animation/Player.h>
#include <Magnum/GL/DefaultFramebuffer.h>
#include <Magnum/GL/Mesh.h>
#include <Magnum/GL/Renderer.h>
#include <Magnum/GL/Texture.h>
#include <Magnum/GL/TextureFormat.h>
#include <Magnum/MeshTools/Compile.h>
#include <Magnum/Platform/Sdl2Application.h>
#include <Magnum/SceneGraph/Camera.h>
#include <Magnum/SceneGraph/Drawable.h>
#include <Magnum/SceneGraph/TranslationRotationScalingTransformation3D.h>
#include <Magnum/SceneGraph/Scene.h>
#include <Magnum/Shaders/Phong.h>
#include <Magnum/Text/Alignment.h>
#include <Magnum/Trade/AbstractImporter.h>
#include <Magnum/Trade/AnimationData.h>
#include <Magnum/Trade/CameraData.h>
#include <Magnum/Trade/ImageData.h>
#include <Magnum/Trade/MeshData3D.h>
#include <Magnum/Trade/MeshObjectData3D.h>
#include <Magnum/Trade/PhongMaterialData.h>
#include <Magnum/Trade/SceneData.h>
#include <Magnum/Trade/TextureData.h>

#include "Magnum/Ui/Anchor.h"
#include "Magnum/Ui/Button.h"
#include "Magnum/Ui/Label.h"
#include "Magnum/Ui/Plane.h"
#include "Magnum/Ui/UserInterface.h"

#ifdef CORRADE_TARGET_EMSCRIPTEN
#include <emscripten/emscripten.h>
#endif

#ifdef MAGNUM_TARGET_WEBGL
#include "Magnum/Ui/Modal.h"
#endif

namespace Magnum {

using namespace Math::Literals;

typedef SceneGraph::Object<SceneGraph::TranslationRotationScalingTransformation3D> Object3D;
typedef SceneGraph::Scene<SceneGraph::TranslationRotationScalingTransformation3D> Scene3D;

constexpr const Float WidgetHeight{36.0f};
constexpr const Float LabelHeight{36.0f};
constexpr const Vector2 ButtonSize{96.0f, WidgetHeight};
constexpr const Vector2 ControlSize{56.0f, WidgetHeight};
constexpr const Vector2 LabelSize{72.0f, LabelHeight};

struct BaseUiPlane: Ui::Plane {
    explicit BaseUiPlane(Ui::UserInterface& ui):
        Ui::Plane{ui, Ui::Snap::Top|Ui::Snap::Bottom|Ui::Snap::Left|Ui::Snap::Right, 1, 50, 640},
        controls{*this, {Ui::Snap::Top|Ui::Snap::Right, ButtonSize}, "Controls", Ui::Style::Success},
        play{*this, {Ui::Snap::Bottom|Ui::Snap::Left, ControlSize}, "Play", Ui::Style::Success},
        pause{*this, {Ui::Snap::Bottom|Ui::Snap::Left, ControlSize}, "Pause", Ui::Style::Warning},
        stop{*this, {Ui::Snap::Right, play, ControlSize}, "Stop", Ui::Style::Danger},
        modelInfo{*this, {Ui::Snap::Top|Ui::Snap::Left, LabelSize}, "", Text::Alignment::LineLeft, 128, Ui::Style::Dim},
        animationProgress{*this, {Ui::Snap::Right, stop, LabelSize}, "", Text::Alignment::LineLeft, 17}
        #ifdef CORRADE_TARGET_EMSCRIPTEN
        ,
        dropHintBackground{*this, {{}, {540, 140}}, Ui::Style::Info},
        dropHint{*this, {{}, {Vector2::yAxis(30.0f), {}}}, "Drag&drop a self-contained glTF file here to view and play it.", Text::Alignment::LineCenter, Ui::Style::Info},
        disclaimer{*this, {{}, {Vector2::yAxis(-10.0f), {}}}, "All data are processed and viewed locally in your\nweb browser. Nothing is uploaded to the server.", Text::Alignment::LineCenter, Ui::Style::Dim}
        #endif
    {
        #ifdef CORRADE_TARGET_EMSCRIPTEN
        Ui::Widget::hide({
            controls,
            play,
            pause,
            stop,
            modelInfo,
            animationProgress});
        #else
        play.hide();
        #endif
    }

    Ui::Button controls,
        play,
        pause,
        stop;
    Ui::Label modelInfo,
        animationProgress;
    #ifdef CORRADE_TARGET_EMSCRIPTEN
    Ui::Modal dropHintBackground;
    Ui::Label dropHint, disclaimer;
    #endif
};

class Player: public Platform::Application, public Interconnect::Receiver {
    public:
        explicit Player(const Arguments& arguments);

        #ifdef CORRADE_TARGET_EMSCRIPTEN
        /* Need to be public to be called from C (which is called from JS) */
        void loadFile(Containers::ArrayView<const char> data);
        #endif

    private:
        void drawEvent() override;
        void viewportEvent(ViewportEvent& event) override;
        void mousePressEvent(MouseEvent& event) override;
        void mouseReleaseEvent(MouseEvent& event) override;
        void mouseMoveEvent(MouseMoveEvent& event) override;
        void mouseScrollEvent(MouseScrollEvent& event) override;

        void toggleControls();
        void play();
        void pause();
        void stop();

        Vector3 positionOnSphere(const Vector2i& position) const;
        void load(Trade::AbstractImporter& importer);

        void addObject(Trade::AbstractImporter& importer, Containers::ArrayView<Object3D*> objects, Containers::ArrayView<const Containers::Optional<Trade::PhongMaterialData>> materials, Object3D& parent, UnsignedInt i);

        Shaders::Phong _coloredShader{{}, 3};
        Shaders::Phong _texturedShader{
            Shaders::Phong::Flag::DiffuseTexture|
            Shaders::Phong::Flag::AlphaMask, 3}; /** @todo remove once I have OIT */

        PluginManager::Manager<Trade::AbstractImporter> _manager;

        struct Data {
            Containers::Array<Containers::Optional<GL::Mesh>> meshes;
            Containers::Array<Containers::Optional<GL::Texture2D>> textures;

            Scene3D scene;
            Object3D manipulator;
            Object3D* cameraObject{};
            SceneGraph::Camera3D* camera;
            SceneGraph::DrawableGroup3D drawables;
            Vector3 previousPosition;

            Containers::Array<char> animationData;
            Animation::Player<std::chrono::nanoseconds, Float> player;

            Int elapsedTimeAnimationDestination;
        };

        const std::pair<Float, Int> _elapsedTimeAnimationData[2] {
            {0.0f, 0},
            {1.0f, 10}
        };
        const Animation::TrackView<Float, Int> _elapsedTimeAnimation{_elapsedTimeAnimationData, Math::lerp};

        Containers::Optional<Data> _data;

        Containers::Optional<Ui::UserInterface> _ui;
        Containers::Optional<BaseUiPlane> _baseUiPlane;
        bool _controlsHidden =
            #ifndef CORRADE_TARGET_EMSCRIPTEN
            false
            #else
            true
            #endif
            ;
};

class ColoredDrawable: public SceneGraph::Drawable3D {
    public:
        explicit ColoredDrawable(Object3D& object, Shaders::Phong& shader, GL::Mesh& mesh, const Color4& color, SceneGraph::DrawableGroup3D& group): SceneGraph::Drawable3D{object, &group}, _shader(shader), _mesh(mesh), _color{color} {}

    private:
        void draw(const Matrix4& transformationMatrix, SceneGraph::Camera3D& camera) override;

        Shaders::Phong& _shader;
        GL::Mesh& _mesh;
        Color4 _color;
};

class TexturedDrawable: public SceneGraph::Drawable3D {
    public:
        explicit TexturedDrawable(Object3D& object, Shaders::Phong& shader, GL::Mesh& mesh, GL::Texture2D& texture, SceneGraph::DrawableGroup3D& group): SceneGraph::Drawable3D{object, &group}, _shader(shader), _mesh(mesh), _texture(texture) {}

    private:
        void draw(const Matrix4& transformationMatrix, SceneGraph::Camera3D& camera) override;

        Shaders::Phong& _shader;
        GL::Mesh& _mesh;
        GL::Texture2D& _texture;
};

#ifdef CORRADE_TARGET_EMSCRIPTEN
Player* app;
#endif

Player::Player(const Arguments& arguments):
    Platform::Application{arguments, Configuration{}
        .setTitle("Magnum Player")
        .setWindowFlags(Configuration::WindowFlag::Resizable)}
{
    Utility::Arguments args;
    #ifndef CORRADE_TARGET_EMSCRIPTEN
    args.addArgument("file").setHelp("file", "file to load")
        .addOption("importer", "TinyGltfImporter").setHelp("importer", "importer plugin to use");
    #endif
    args.addSkippedPrefix("magnum").setHelp("engine-specific options")
        .setHelp("Displays a 3D scene file provided on command line.")
        .parse(arguments.argc, arguments.argv);

    /* Setup renderer and shader defaults */
    GL::Renderer::enable(GL::Renderer::Feature::DepthTest);
    GL::Renderer::enable(GL::Renderer::Feature::FaceCulling);
    _coloredShader
        .setAmbientColor(0x111111_rgbf)
        .setSpecularColor(0xffffff_rgbf)
        .setShininess(80.0f);
    _texturedShader
        .setAmbientColor(0x00000000_rgbaf)
        .setSpecularColor(0x11111100_rgbaf)
        .setShininess(80.0f);

    /* Setup the UI */
    _ui.emplace(Vector2(windowSize())/dpiScaling(), windowSize(), framebufferSize(), Ui::mcssDarkStyleConfiguration());
    _baseUiPlane.emplace(*_ui);
    Interconnect::connect(_baseUiPlane->controls, &Ui::Button::tapped, *this, &Player::toggleControls);
    Interconnect::connect(_baseUiPlane->play, &Ui::Button::tapped, *this, &Player::play);
    Interconnect::connect(_baseUiPlane->pause, &Ui::Button::tapped, *this, &Player::pause);
    Interconnect::connect(_baseUiPlane->stop, &Ui::Button::tapped, *this, &Player::stop);

    #ifndef CORRADE_TARGET_EMSCRIPTEN
    /* Load a scene importer plugin */
    std::unique_ptr<Trade::AbstractImporter> importer =
        _manager.loadAndInstantiate(args.value("importer"));
    if(!importer) std::exit(1);

    Debug{} << "Opening file" << args.value("file");

    /* Load file */
    if(!importer->openFile(args.value("file")))
        std::exit(4);

    load(*importer);
    #endif

    setSwapInterval(1);
    #ifdef CORRADE_TARGET_EMSCRIPTEN
    app = this;
    #endif
}

void Player::toggleControls() {
    if(_controlsHidden) {
        if(_data) {
            if(!_data->player.isEmpty()) {
                if(_data->player.state() == Animation::State::Playing) {
                    _baseUiPlane->play.hide();
                    _baseUiPlane->pause.show();
                } else {
                    _baseUiPlane->play.show();
                    _baseUiPlane->pause.hide();
                }
                Ui::Widget::show({
                    _baseUiPlane->stop,
                    _baseUiPlane->animationProgress});
            }

            _baseUiPlane->modelInfo.show();
        }

        _baseUiPlane->controls.setStyle(Ui::Style::Success);
        _controlsHidden = false;

    } else {
        Ui::Widget::hide({
            _baseUiPlane->play,
            _baseUiPlane->pause,
            _baseUiPlane->stop,
            _baseUiPlane->modelInfo,
            _baseUiPlane->animationProgress});

        _baseUiPlane->controls.setStyle(Ui::Style::Flat);
        _controlsHidden = true;
    }
}

void Player::play() {
    if(!_data) return;

    _baseUiPlane->play.hide();
    _baseUiPlane->pause.show();
    _baseUiPlane->stop.enable();
    _data->player.play(std::chrono::system_clock::now().time_since_epoch());
}

void Player::pause() {
    if(!_data) return;

    _baseUiPlane->play.show();
    _baseUiPlane->pause.hide();
    _data->player.pause(std::chrono::system_clock::now().time_since_epoch());
}

void Player::stop() {
    if(!_data) return;

    _data->player.stop();

    _baseUiPlane->play.show();
    _baseUiPlane->pause.hide();
    _baseUiPlane->stop.disable();
}

#ifdef CORRADE_TARGET_EMSCRIPTEN
void Player::loadFile(Containers::ArrayView<const char> data) {
    std::unique_ptr<Trade::AbstractImporter> importer =
        _manager.loadAndInstantiate("TinyGltfImporter");
    if(!importer) std::exit(1);

    Debug{} << "Opening D&D data";

    /* Load file */
    if(!importer->openData(data))
        std::exit(4);

    load(*importer);

    Ui::Widget::hide({
        _baseUiPlane->dropHintBackground,
        _baseUiPlane->dropHint,
        _baseUiPlane->disclaimer});
    _baseUiPlane->controls.show();

    redraw();
}
#endif

void Player::load(Trade::AbstractImporter& importer) {
    _data.emplace();

    /* Base object, parent of all (for easy manipulation) */
    _data->manipulator.setParent(&_data->scene);

    /* Load all textures. Textures that fail to load will be NullOpt. */
    _data->textures = Containers::Array<Containers::Optional<GL::Texture2D>>{importer.textureCount()};
    for(UnsignedInt i = 0; i != importer.textureCount(); ++i) {
        Debug{} << "Importing texture" << i << importer.textureName(i);

        Containers::Optional<Trade::TextureData> textureData = importer.texture(i);
        if(!textureData || textureData->type() != Trade::TextureData::Type::Texture2D) {
            Warning{} << "Cannot load texture properties, skipping";
            continue;
        }

        Debug{} << "Importing image" << textureData->image() << importer.image2DName(textureData->image());

        Containers::Optional<Trade::ImageData2D> imageData = importer.image2D(textureData->image());
        GL::TextureFormat format;
        if(imageData && imageData->format() == PixelFormat::RGB8Unorm) {
            #ifndef MAGNUM_TARGET_GLES2
            format = GL::TextureFormat::RGB8;
            #else
            format = GL::TextureFormat::RGB;
            #endif
        } else if(imageData && imageData->format() == PixelFormat::RGBA8Unorm) {
            #ifndef MAGNUM_TARGET_GLES2
            format = GL::TextureFormat::RGBA8;
            #else
            format = GL::TextureFormat::RGBA;
            #endif
        } else {
            Warning{} << "Cannot load texture image, skipping";
            continue;
        }

        /* Configure the texture */
        GL::Texture2D texture;
        texture
            .setMagnificationFilter(textureData->magnificationFilter())
            .setMinificationFilter(textureData->minificationFilter(), textureData->mipmapFilter())
            .setWrapping(textureData->wrapping().xy())
            .setStorage(Math::log2(imageData->size().max()) + 1, format, imageData->size())
            .setSubImage(0, {}, *imageData)
            .generateMipmap();

        _data->textures[i] = std::move(texture);
    }

    /* Load all materials. Materials that fail to load will be NullOpt. The
       data will be stored directly in objects later, so save them only
       temporarily. */
    Containers::Array<Containers::Optional<Trade::PhongMaterialData>> materials{importer.materialCount()};
    for(UnsignedInt i = 0; i != importer.materialCount(); ++i) {
        Debug{} << "Importing material" << i << importer.materialName(i);

        std::unique_ptr<Trade::AbstractMaterialData> materialData = importer.material(i);
        if(!materialData || materialData->type() != Trade::MaterialType::Phong) {
            Warning{} << "Cannot load material, skipping";
            continue;
        }

        materials[i] = std::move(static_cast<Trade::PhongMaterialData&>(*materialData));
    }

    /* Load all meshes. Meshes that fail to load will be NullOpt. */
    _data->meshes = Containers::Array<Containers::Optional<GL::Mesh>>{importer.mesh3DCount()};
    for(UnsignedInt i = 0; i != importer.mesh3DCount(); ++i) {
        Debug{} << "Importing mesh" << i << importer.mesh3DName(i);

        Containers::Optional<Trade::MeshData3D> meshData = importer.mesh3D(i);
        if(!meshData || meshData->primitive() != MeshPrimitive::Triangles) {
            Warning{} << "Cannot load the mesh, skipping";
            continue;
        }

        /** @todo do something about this? */
        if(!meshData->normalArrayCount())
            Warning{} << "The mesh doesn't have normals, might render improperly";

        /* Compile the mesh */
        _data->meshes[i] = MeshTools::compile(*meshData);
    }

    /* Load the scene. Save the object pointers in an array for easier mapping
       of animations later. */
    Containers::Array<Object3D*> objects{Containers::ValueInit, importer.object3DCount()};
    if(importer.defaultScene() != -1) {
        Debug{} << "Adding default scene" << importer.sceneName(importer.defaultScene());

        Containers::Optional<Trade::SceneData> sceneData = importer.scene(importer.defaultScene());
        if(!sceneData) {
            Error{} << "Cannot load scene, exiting";
            return;
        }

        /* Recursively add all children */
        for(UnsignedInt objectId: sceneData->children3D())
            addObject(importer, objects, materials, _data->manipulator, objectId);

    /* The format has no scene support, display just the first loaded mesh with
       a default material and be done with it */
    } else if(!_data->meshes.empty() && _data->meshes[0])
        new ColoredDrawable{_data->manipulator, _coloredShader, *_data->meshes[0], 0xffffff_rgbf, _data->drawables};

    /* Create a camera object in case it wasn't present in the scene already */
    if(!_data->cameraObject) {
        _data->cameraObject = new Object3D{&_data->scene};
        _data->cameraObject->translate(Vector3::zAxis(5.0f));
    }

    /* Basic camera setup */
    (*(_data->camera = new SceneGraph::Camera3D{*_data->cameraObject}))
        .setAspectRatioPolicy(SceneGraph::AspectRatioPolicy::Extend)
        .setProjectionMatrix(Matrix4::perspectiveProjection(75.0_degf, 1.0f, 0.01f, 1000.0f))
        .setViewport(GL::defaultFramebuffer.viewport().size());

    /* Use the settings with parameters of the camera in the model, if any,
       otherwise just used the hardcoded setup from above */
    if(importer.cameraCount()) {
        Containers::Optional<Trade::CameraData> camera = importer.camera(0);
        if(camera) _data->camera->setProjectionMatrix(Matrix4::perspectiveProjection(camera->fov(), 1.0f, camera->near(), camera->far()));
    }

    /* Import animations */
    for(UnsignedInt i = 0; i != importer.animationCount(); ++i) {
        Debug{} << "Importing animation" << i << importer.animationName(i);

        Containers::Optional<Trade::AnimationData> animation = importer.animation(i);
        if(!animation) {
            Warning{} << "Cannot load the animation, skipping";
            continue;
        }

        for(UnsignedInt j = 0; j != animation->trackCount(); ++j) {
            if(animation->trackTargetId(j) >= objects.size() || !objects[animation->trackTargetId(j)])
                continue;

            Object3D& animatedObject = *objects[animation->trackTargetId(j)];

            if(animation->trackTarget(j) == Trade::AnimationTrackTarget::Translation3D) {
                const auto callback = [](const Float&, const Vector3& translation, Object3D& object) {
                    object.setTranslation(translation);
                };
                if(animation->trackType(j) == Trade::AnimationTrackType::CubicHermite3D) {
                    _data->player.addWithCallback(animation->track<CubicHermite3D>(j),
                        callback, animatedObject);
                } else {
                    CORRADE_INTERNAL_ASSERT(animation->trackType(j) == Trade::AnimationTrackType::Vector3);
                    _data->player.addWithCallback(animation->track<Vector3>(j),
                        callback, animatedObject);
                }
            } else if(animation->trackTarget(j) == Trade::AnimationTrackTarget::Rotation3D) {
                const auto callback = [](const Float&, const Quaternion& rotation, Object3D& object) {
                    object.setRotation(rotation);
                };
                if(animation->trackType(j) == Trade::AnimationTrackType::CubicHermiteQuaternion) {
                    _data->player.addWithCallback(animation->track<CubicHermiteQuaternion>(j),
                        callback, animatedObject);
                } else {
                    CORRADE_INTERNAL_ASSERT(animation->trackType(j) == Trade::AnimationTrackType::Quaternion);
                    _data->player.addWithCallback(animation->track<Quaternion>(j),
                        callback, animatedObject);
                }
            } else if(animation->trackTarget(j) == Trade::AnimationTrackTarget::Scaling3D) {
                const auto callback = [](const Float&, const Vector3& scaling, Object3D& object) {
                    object.setScaling(scaling);
                };
                if(animation->trackType(j) == Trade::AnimationTrackType::CubicHermite3D) {
                    _data->player.addWithCallback(animation->track<CubicHermite3D>(j),
                        callback, animatedObject);
                } else {
                    CORRADE_INTERNAL_ASSERT(animation->trackType(j) == Trade::AnimationTrackType::Vector3);
                    _data->player.addWithCallback(animation->track<Vector3>(j),
                        callback, animatedObject);
                }
            } else CORRADE_ASSERT_UNREACHABLE();
        }
        _data->animationData = animation->release();

        /* Load only the first animation at the moment */
        break;
    }

    /* Populate the model info */
    _baseUiPlane->modelInfo.setText(Utility::formatString(
        "{} objs, {} meshes, {} texs, {} anims",
        importer.object3DCount(),
        importer.mesh3DCount(),
        importer.textureCount(),
        importer.animationCount()));

    if(!_data->player.isEmpty()) {
        /* Animate the elapsed time -- trigger update every 1/10th a second */
        _data->player.addWithCallbackOnChange(_elapsedTimeAnimation, [](const Float&, const Int& elapsed, Player& player) {
            if(player._baseUiPlane->animationProgress.flags() & Ui::WidgetFlag::Hidden) return;
            const Int duration = player._data->player.duration().size()[0]*10;
            player._baseUiPlane->animationProgress.setText(Utility::formatString(
                "{:.2}:{:.2}.{:.1} / {:.2}:{:.2}.{:.1}",
                elapsed/600, elapsed/10%60, elapsed%10,
                duration/600, duration/10%60, duration%10));
        }, _data->elapsedTimeAnimationDestination, *this);

        /* Start the animation */
        _data->player.setPlayCount(0)
            .play(std::chrono::system_clock::now().time_since_epoch());
    }

    _controlsHidden = true;
    toggleControls();
}

void Player::addObject(Trade::AbstractImporter& importer, Containers::ArrayView<Object3D*> objects, Containers::ArrayView<const Containers::Optional<Trade::PhongMaterialData>> materials, Object3D& parent, UnsignedInt i) {
    Debug{} << "Importing object" << i << importer.object3DName(i);
    std::unique_ptr<Trade::ObjectData3D> objectData = importer.object3D(i);
    if(!objectData) {
        Error{} << "Cannot import object, skipping";
        return;
    }

    /* Add the object to the scene and set its transformation. If it has a
       separate TRS, use that to avoid precision issues. */
    auto* object = new Object3D{&parent};
    if(objectData->flags() & Trade::ObjectFlag3D::HasTranslationRotationScaling)
        (*object).setTranslation(objectData->translation())
                 .setRotation(objectData->rotation())
                 .setScaling(objectData->scaling());
    else object->setTransformation(objectData->transformation());

    /* Save it to the ID -> pointer mapping array for animation targets */
    objects[i] = object;

    /* Add a drawable if the object has a mesh and the mesh is loaded */
    if(objectData->instanceType() == Trade::ObjectInstanceType3D::Mesh && objectData->instance() != -1 && _data->meshes[objectData->instance()]) {
        const Int materialId = static_cast<Trade::MeshObjectData3D*>(objectData.get())->material();

        /* Material not available / not loaded, use a default material */
        if(materialId == -1 || !materials[materialId]) {
            new ColoredDrawable{*object, _coloredShader, *_data->meshes[objectData->instance()], 0xffffff_rgbf, _data->drawables};

        /* Textured material. If the texture failed to load, again just use a
           default colored material. */
        } else if(materials[materialId]->flags() & Trade::PhongMaterialData::Flag::DiffuseTexture) {
            Containers::Optional<GL::Texture2D>& texture = _data->textures[materials[materialId]->diffuseTexture()];
            if(texture)
                new TexturedDrawable{*object, _texturedShader, *_data->meshes[objectData->instance()], *texture, _data->drawables};
            else
                new ColoredDrawable{*object, _coloredShader, *_data->meshes[objectData->instance()], 0xffffff_rgbf, _data->drawables};

        /* Color-only material */
        } else {
            new ColoredDrawable{*object, _coloredShader, *_data->meshes[objectData->instance()], materials[materialId]->diffuseColor(), _data->drawables};
        }

    /* This is a node that holds the default camera -> assign the object to the
       global camera pointer */
    } else if(objectData->instanceType() == Trade::ObjectInstanceType3D::Camera && objectData->instance() == 0) {
        _data->cameraObject = object;
    }

    /* Recursively add children */
    for(std::size_t id: objectData->children())
        addObject(importer, objects, materials, *object, id);
}

void ColoredDrawable::draw(const Matrix4& transformationMatrix, SceneGraph::Camera3D& camera) {
    _shader
        .setDiffuseColor(_color)
        .setLightPositions({
            /** @todo make this configurable, deduplicate and calculate only once */
            camera.cameraMatrix().transformPoint(Vector3{10.0f, 10.0f, 10.0f}*100.0f),
            camera.cameraMatrix().transformPoint(Vector3{-5.0f, -5.0f, 10.0f}*100.0f),
            camera.cameraMatrix().transformPoint(Vector3{0.0f, 10.0f, -10.0f}*100.0f)})
        .setLightColors({0xffffff_rgbf, 0xff9999_rgbf, 0x9999ff_rgbf})
        .setTransformationMatrix(transformationMatrix)
        .setNormalMatrix(transformationMatrix.rotationScaling())
        .setProjectionMatrix(camera.projectionMatrix());

    _mesh.draw(_shader);
}

void TexturedDrawable::draw(const Matrix4& transformationMatrix, SceneGraph::Camera3D& camera) {
    _shader
        .setLightPositions({
            /** @todo make this configurable, deduplicate and calculate only once */
            camera.cameraMatrix().transformPoint(Vector3{10.0f, 10.0f, 10.0f}*100.0f),
            camera.cameraMatrix().transformPoint(Vector3{-5.0f, -5.0f, 10.0f}*100.0f),
            camera.cameraMatrix().transformPoint(Vector3{0.0f, 30.0f, -10.0f}*100.0f)})
        .setLightColors({0xffffff_rgbf, 0xff9999_rgbf, 0x9999ff_rgbf})
        .setTransformationMatrix(transformationMatrix)
        .setNormalMatrix(transformationMatrix.rotationScaling())
        .setProjectionMatrix(camera.projectionMatrix())
        .bindDiffuseTexture(_texture);

    _mesh.draw(_shader);
}

void Player::drawEvent() {
    GL::defaultFramebuffer.clear(GL::FramebufferClear::Color|GL::FramebufferClear::Depth);

    if(_data) {
        _data->player.advance(std::chrono::system_clock::now().time_since_epoch());

        _data->camera->draw(_data->drawables);
    }

    /* Draw the UI. Disable the depth buffer and enable premultiplied alpha
       blending. */
    {
        GL::Renderer::disable(GL::Renderer::Feature::DepthTest);
        GL::Renderer::enable(GL::Renderer::Feature::Blending);
        GL::Renderer::setBlendFunction(GL::Renderer::BlendFunction::One, GL::Renderer::BlendFunction::OneMinusSourceAlpha);
        _ui->draw();
        GL::Renderer::setBlendFunction(GL::Renderer::BlendFunction::One, GL::Renderer::BlendFunction::Zero);
        GL::Renderer::disable(GL::Renderer::Feature::Blending);
        GL::Renderer::enable(GL::Renderer::Feature::DepthTest);
    }

    /* Schedule a redraw only if the player is playing to avoid hogging the
       CPU */
    if(_data && _data->player.state() == Animation::State::Playing) redraw();

    swapBuffers();
}

void Player::viewportEvent(ViewportEvent& event) {
    GL::defaultFramebuffer.setViewport({{}, event.framebufferSize()});
    if(_data) _data->camera->setViewport(event.framebufferSize());
}

void Player::mousePressEvent(MouseEvent& event) {
    if(_ui->handlePressEvent(event.position())) {
        redraw();
        return;
    }

    if(_data && event.button() == MouseEvent::Button::Left)
        _data->previousPosition = positionOnSphere(event.position());
}

void Player::mouseReleaseEvent(MouseEvent& event) {
    if(_ui->handleReleaseEvent(event.position())) {
        redraw();
        return;
    }

    if(_data && event.button() == MouseEvent::Button::Left)
        _data->previousPosition = Vector3();
}

void Player::mouseScrollEvent(MouseScrollEvent& event) {
    if(!_data || !event.offset().y()) return;

    /* Distance to origin */
    const Float distance = _data->cameraObject->transformation().translation().z();

    /* Move 15% of the distance back or forward */
    _data->cameraObject->translate(Vector3::zAxis(
        distance*(1.0f - (event.offset().y() > 0 ? 1/0.85f : 0.85f))));

    redraw();
}

Vector3 Player::positionOnSphere(const Vector2i& position) const {
    const Vector2 positionNormalized = Vector2{position}/Vector2{_data->camera->viewport()} - Vector2{0.5f};
    const Float length = positionNormalized.length();
    const Vector3 result(length > 1.0f ? Vector3(positionNormalized, 0.0f) : Vector3(positionNormalized, 1.0f - length));
    return (result*Vector3::yScale(-1.0f)).normalized();
}

void Player::mouseMoveEvent(MouseMoveEvent& event) {
    if(_ui->handleMoveEvent(event.position())) {
        redraw();
        return;
    }

    if(!_data || !(event.buttons() & MouseMoveEvent::Button::Left)) return;

    const Vector3 currentPosition = positionOnSphere(event.position());
    const Vector3 axis = Math::cross(_data->previousPosition, currentPosition);

    if(_data->previousPosition.length() < 0.001f || axis.length() < 0.001f) return;

    _data->manipulator.rotate(Math::angle(_data->previousPosition, currentPosition), axis.normalized());
    _data->previousPosition = currentPosition;

    redraw();
}

}

#ifdef CORRADE_TARGET_EMSCRIPTEN
extern "C" {
    EMSCRIPTEN_KEEPALIVE void loadFile(const char* ptr, const std::size_t size);
    void loadFile(const char* ptr, const std::size_t size) {
        Magnum::app->loadFile({ptr, size});
    }
}
#endif

MAGNUM_APPLICATION_MAIN(Magnum::Player)
